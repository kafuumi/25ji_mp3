#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "amp/amp_event.h"
#include "amp/amp_mem.h"
#include "amp/file_reader.h"
#include "element_priv.h"
#include "esp_log.h"

#define AMP_FILE_READER_EVENT_WAIT_TICKS pdMS_TO_TICKS(100)
#define AMP_FILE_READER_WRITE_WAIT_TICKS pdMS_TO_TICKS(3000)
#define AMP_FILE_READER_POST_WAIT_TICKS pdMS_TO_TICKS(1000)
#define AMP_FILE_READER_WRITE_RETRY_COUNT 3

static const char *TAG = "file_reader";

struct file_reader {
    AMP_ELEMENT_ENTRY() el_entry;
    ringbuf_handle_t rb;
    amp_playlist_handle_t playlist;
};

typedef struct {
    TickType_t event_wait_ticks;
    int cur_fd;
    amp_track_handle_t cur_track;
    enum amp_state cached_state;
    bool waiting_eos_done;
    bool stop_requested;
} amp_file_reader_task_state_t;

static void amp_file_reader_set_output(void *args, ringbuf_handle_t rb) {
    amp_file_reader_handle_t reader = args;
    reader->rb = rb;
}

static bool amp_file_reader_report_eos(amp_file_reader_handle_t reader) {
    esp_err_t err = esp_event_post_to(reader->el_entry.event_bus, AMP_EVENT_REPORT, AMP_EVENT_REPORT_STREAM_EOS, 0, 0,
                                      AMP_FILE_READER_POST_WAIT_TICKS);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "failed to post EOS event: %d(%s)", err, esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "posted EOS event");
    AMP_EL_SEND_DONE(TAG, reader, el_entry);
    return true;
}

static bool amp_file_reader_process_notify(amp_file_reader_handle_t reader, amp_file_reader_task_state_t *state) {
    uint32_t notify = 0;
    if (xTaskNotifyWait(0, ULONG_MAX, &notify, state->event_wait_ticks) == pdTRUE) {
        if (notify & NOTIFY_VALUE_MASK_STATE) {
            state->cached_state = AMP_DASH_LOAD_STATE(reader->el_entry.dashboard);
        }
        if (notify & NOTIFY_VALUE_MASK_EOS_DONE) {
            state->waiting_eos_done = false;
        }
    }
    bool should_wait;
    if (state->waiting_eos_done) {
        should_wait = true;
    } else {
        should_wait = state->cached_state != AMP_STATE_PLAYING;
    }
    if (should_wait) {
        if (state->event_wait_ticks <= 0) {
            state->event_wait_ticks = AMP_FILE_READER_EVENT_WAIT_TICKS;
        }
    } else if (state->event_wait_ticks > 0) {
        state->event_wait_ticks = 0;
    }
    return should_wait;
}

static bool amp_file_reader_open_file(amp_file_reader_handle_t reader, amp_file_reader_task_state_t *state) {
    amp_track_handle_t track = amp_playlist_next(reader->playlist);
    if (!track) {
        ESP_LOGW(TAG, "no more file to read");
        return false;
    }
    if (track->is_dir) {
        ESP_LOGW(TAG, "skipping directory: %s", track->path);
        return false;
    }
    const char *name = track->path;
    int fd = open(name, O_RDONLY);
    if (fd <= 0) {
        ESP_LOGE(TAG, "failed to open file %s: %d(%s)", name, errno, strerror(errno));
        return false;
    }
    ESP_LOGI(TAG, "opened file %s (fd=%d, type=%d)", name, fd, track->media_type);
    AMP_DASH_SET_MEDIA_TYPE(reader->el_entry.dashboard, track->media_type);
    state->cur_fd = fd;
    state->cur_track = track;

    esp_err_t err = esp_event_post_to(reader->el_entry.event_bus, AMP_EVENT_REPORT, AMP_EVENT_REPORT_AUDIO_FORMAT, 0, 0,
                                      AMP_FILE_READER_POST_WAIT_TICKS);
    ESP_LOGI(TAG, "open new stream, post AUDIO FORMAT event");
    if (ESP_OK != err) {
        ESP_LOGW(TAG, "failed to post audio format event: %d(%s)", err, esp_err_to_name(err));
    }
    return true;
}

static void amp_file_reader_task(void *args) {
    amp_file_reader_handle_t reader = args;
    ringbuf_handle_t rb = reader->rb;
    assert(rb);

    size_t buf_size = 1024;
    uint8_t *buf = amp_malloc(sizeof(uint8_t) * buf_size);
    TickType_t write_wait_ticks = pdMS_TO_TICKS(1000);

    amp_file_reader_task_state_t task_state = {
        .cur_fd = 0,
        .cur_track = NULL,
        .event_wait_ticks = AMP_FILE_READER_EVENT_WAIT_TICKS,
        .cached_state = AMP_DASH_LOAD_STATE(reader->el_entry.dashboard),
        .waiting_eos_done = false,
        .stop_requested = false,
    };

    while (true) {
        if (task_state.stop_requested) {
            goto _task_end;
        }
        if (amp_file_reader_process_notify(reader, &task_state)) {
            continue;
        }
        if (task_state.cur_fd <= 0 && !amp_file_reader_open_file(reader, &task_state)) {
            continue;
        }
        ssize_t read_size = read(task_state.cur_fd, buf, buf_size);
        if (read_size < 0) {
            ESP_LOGE(TAG, "failed to read file %s: %d(%s)", task_state.cur_track->path, errno, strerror(errno));
            goto _task_end;
        } else if (read_size == 0) {
            ESP_LOGI(TAG, "reached EOF: %s", task_state.cur_track->path);
            rb_done_write(rb);
            task_state.waiting_eos_done = true;
            task_state.cur_track = NULL;
            close(task_state.cur_fd);
            task_state.cur_fd = 0;
            amp_file_reader_report_eos(reader);
            continue;
        }
        ESP_LOGD(TAG, "read file %s success, size: %d", task_state.cur_track->path, read_size);

        int write_size;
        int retry = 0;
    _retry_write:
        write_size = rb_write(rb, (char *)buf, read_size, write_wait_ticks);
        if (RB_DONE == write_size) {
            ESP_LOGW(TAG, "output ringbuf done write");
        } else if (RB_ABORT == write_size) {
            ESP_LOGW(TAG, "output ringbuf aborted");
        } else if (RB_TIMEOUT == write_size) {
            retry++;
            if (retry < AMP_FILE_READER_WRITE_RETRY_COUNT) {
                ESP_LOGI(TAG, "write to ringbuf timeout, retry: %d", retry);
                goto _retry_write;
            } else {
                ESP_LOGW(TAG, "write to ringbuf failed after %d retries", retry);
                continue;
            }
        } else if (write_size <= 0) {
            ESP_LOGE(TAG, "failed to write to ringbuf: %d", write_size);
        } else {
            ESP_LOGD(TAG, "wrote to ringbuf: %d bytes", write_size);
        }
    }

_task_end:
    if (buf) {
        amp_free(buf);
    }
    vTaskDelete(NULL);
}

static void amp_file_reader_el_deinit(void *args) { amp_file_reader_deinit((amp_file_reader_handle_t)args); }

static const amp_element_interface_t amp_file_reader_element_interface = {
    .deinit = amp_file_reader_el_deinit,
    .set_input_rb = NULL,
    .set_output_rb = amp_file_reader_set_output,
    .run_task = amp_file_reader_task,
    .register_events = NULL,
};

const amp_element_interface_t *amp_file_reader_get_element_interface() { return &amp_file_reader_element_interface; }

esp_err_t amp_file_reader_init(amp_file_reader_cfg_t *cfg, amp_file_reader_handle_t *fr) {
    amp_file_reader_handle_t f = amp_calloc(1, sizeof(struct file_reader));
    if (!f) {
        return ESP_ERR_NO_MEM;
    }
    f->playlist = cfg->playlist;
    *fr = f;
    return ESP_OK;
}

void amp_file_reader_deinit(amp_file_reader_handle_t fr) {
    if (!fr)
        return;
    amp_free(fr);
}
