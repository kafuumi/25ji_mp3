#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <unistd.h>

#include "amp/amp_event.h"
#include "amp/amp_mem.h"
#include "amp/file_reader.h"
#include "element_priv.h"
#include "esp_log.h"

#include "sds.h"

#define MAX_WAIT_TIME_EVENT pdMS_TO_TICKS(100)
#define MAX_WAIT_TIME_WRITE pdMS_TO_TICKS(3000)
#define MAX_WAIT_TIME_POST_EVENT pdMS_TO_TICKS(1000)

#define FILE_TYPE_NAME_MP3 ".mp3"
#define FILE_TYPE_NAME_AAC ".aac"
#define FILE_TYPE_NAME_FLAC ".flac"

#define DO_WHAT_ON_FAIL(fail_counter)                                                                                  \
    do {                                                                                                               \
        ++fail_counter;                                                                                                \
        if (fail_counter > 3) {                                                                                        \
        }                                                                                                              \
    } while (0)

#define AUDIO_FILE_NODE_CREATE(var, err, type, on_fail)                                                                \
    do {                                                                                                               \
        var = amp_malloc(sizeof(struct audio_file_source_node));                                                       \
        if (!var) {                                                                                                    \
            err = ESP_ERR_NO_MEM;                                                                                      \
            break;                                                                                                     \
        }                                                                                                              \
        var->source.media_type = type;                                                                                 \
    } while (0)

static const char *TAG = "file_reader";

struct audio_file_source_node {
    struct audio_file_source source;
    TAILQ_ENTRY(audio_file_source_node) tailq_entry;
};

typedef TAILQ_HEAD(audio_file_source_head, audio_file_source_node) audio_file_source_head_t;

struct file_reader {
    AMP_ELEMENT_ENTRY() el_entry;
    sds base;
    size_t size;
    struct audio_file_source_node *cur;
    audio_file_source_head_t file_list;
    ringbuf_handle_t rb;
};

static void file_reader_set_output(void *args, ringbuf_handle_t rb) {
    file_reader_handle_t reader = args;
    reader->rb = rb;
}

struct file_reader_task_state {
    TickType_t wait_event_time;
    int cur_fd;
    const struct audio_file_source *cur_source;
    enum amp_state state;
    bool wait_next;
    bool task_stop;
};

inline static bool send_eos_event(file_reader_handle_t reader) {
    esp_err_t err = esp_event_post_to(reader->el_entry.event_bus, AMP_EVENT_REPORT, AMP_EVENT_REPORT_EOS, 0, 0,
                                      MAX_WAIT_TIME_POST_EVENT);
    ESP_LOGD(TAG, "post EOS event");
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "post EOS event fail: %d(%s)", err, esp_err_to_name(err));
        return false;
    }
    AMP_EL_SEND_DONE(TAG, reader, el_entry);
    return true;
}

inline static bool file_reader_receive_event(file_reader_handle_t reader, struct file_reader_task_state *task_state) {
    uint32_t notify = 0;
    if (xTaskNotifyWait(0, ULONG_MAX, &notify, task_state->wait_event_time) == pdTRUE) {
        if (notify & NOTIFY_VALUE_MASK_STATE) {
            task_state->state = AMP_DASH_LOAD_STATE(reader->el_entry.dashboard);
        }
        if (notify & NOTIFY_VALUE_MASK_EOS_DONE) {
            task_state->wait_next = false;
        }
    }
    bool notplay;
    enum amp_state state = task_state->state;

    if (task_state->wait_next) {
        notplay = true;
    } else {
        notplay = state != AMP_STATE_PLAYING;
    }
    if (notplay) {
        if (task_state->wait_event_time <= 0) {
            task_state->wait_event_time = MAX_WAIT_TIME_EVENT;
        }
    } else if (task_state->wait_event_time > 0) {
        task_state->wait_event_time = 0;
    }
    return notplay;
}

inline static bool file_reader_open_file(file_reader_handle_t reader, struct file_reader_task_state *task_state) {
    const struct audio_file_source *src = file_reader_next(reader);
    if (!src) {
        ESP_LOGW(TAG, "no more file to read");
        return false;
    }
    if (src->is_dir) {
        ESP_LOGW(TAG, "current file %s is dir, skip", src->name);
        return false;
    }
    const char *name = src->name;
    int fd = open(name, O_RDONLY);
    if (fd <= 0) {
        ESP_LOGE(TAG, "open file %s fail: %d(%s)", name, errno, strerror(errno));
        return false;
    }
    struct stat stat_file;
    stat(src->name, &stat_file);
    ESP_LOGI(TAG, "file %s st_mode: %d", src->name, stat_file.st_mode);
    ESP_LOGI(TAG, "open file %s success, fd: %d, type: %d", name, fd, src->media_type);
    AMP_DASH_SET_MEDIA_TYPE(reader->el_entry.dashboard, src->media_type);
    task_state->cur_fd = fd;
    task_state->cur_source = src;
    esp_event_post_to(reader->el_entry.event_bus, AMP_EVENT_REPORT, AMP_EVENT_REPORT_MEDIA_TYPE, 0, 0,
                      MAX_WAIT_TIME_POST_EVENT);
    return true;
}

static void file_reader_task_run(void *args) {
    file_reader_handle_t reader = args;
    ringbuf_handle_t rb = reader->rb;
    assert(rb);

    size_t buf_size = 1024;
    uint8_t *buf = amp_malloc(sizeof(uint8_t) * buf_size);
    TickType_t wait_time = pdMS_TO_TICKS(1000);

    struct file_reader_task_state task_state = {
        .cur_fd = 0,
        .cur_source = NULL,
        .wait_event_time = MAX_WAIT_TIME_EVENT,
        .state = AMP_DASH_LOAD_STATE(reader->el_entry.dashboard),
        .wait_next = false,
        .task_stop = false,
    };
    int fail_counter = 0;

    while (true) {
        /* receive event */
        if (task_state.task_stop) {
            goto _task_end;
        }
        if (file_reader_receive_event(reader, &task_state)) {
            continue;
        }
        /* open file */
        if (task_state.cur_fd <= 0 && !file_reader_open_file(reader, &task_state)) {
            continue;
        }
        /* read data from file */
        ssize_t read_size = read(task_state.cur_fd, buf, buf_size);
        if (read_size < 0) {
            // read error
            ESP_LOGE(TAG, "read file %s fail: %d(%s)", task_state.cur_source->name, errno, strerror(errno));
            DO_WHAT_ON_FAIL(fail_counter);
            goto _task_end;
            continue;
        } else if (read_size == 0) {
            // read finished
            ESP_LOGI(TAG, "read file %s EOF", task_state.cur_source->name);
            rb_done_write(rb);
            task_state.wait_next = true;
            task_state.cur_source = NULL;
            close(task_state.cur_fd);
            task_state.cur_fd = 0;
            send_eos_event(reader);
            continue;
        }
        ESP_LOGD(TAG, "read file %s success, size: %d", task_state.cur_source->name, read_size);
        /* send data to ringbuf */
        int write_size;
        int retry_count = 0;

    _try_write:
        write_size = rb_write(rb, (char *)buf, read_size, wait_time);
        if (RB_DONE == write_size) {
            /* unreachable */
            ESP_LOGW(TAG, "output ringbuf is done write");
        } else if (RB_ABORT == write_size) {
            /* unreachable */
            ESP_LOGW(TAG, "output ringbuf is abort write");
        } else if (RB_TIMEOUT == write_size) {
            if (retry_count < 3) {
                ESP_LOGI(TAG, "write to ringbuf timeout, retry");
                goto _try_write;
            } else {
                ESP_LOGW(TAG, "retry write to ringbuf timeout fail, try count: %d", retry_count);
                continue;
            }
        } else if (write_size <= 0) {
            ESP_LOGE(TAG, "send data to ringbuf fail: %d", write_size);
        } else {
            ESP_LOGD(TAG, "send data to output ringbuf success, size: %d", write_size);
        }
    }

_task_end:
    if (buf) {
        amp_free(buf);
    }
    vTaskDelete(NULL);
}

static void file_reader_el_deinit(void *args) { file_reader_deinit((file_reader_handle_t)args); }

static amp_element_interface_t file_reader_element_interface = {
    .deinit = file_reader_el_deinit,
    .set_input_rb = NULL,
    .set_output_rb = file_reader_set_output,
    .task_run = file_reader_task_run,
};

const amp_element_interface_t *file_reader_el_interface() { return &file_reader_element_interface; }

struct audio_file_source *file_reader_next(file_reader_handle_t fl) {
    if (fl->size == 0) {
        ESP_LOGE(TAG, "audio file list is empty");
        return NULL;
    }
    struct audio_file_source *ret = NULL;
    if (fl->cur == NULL)
        fl->cur = TAILQ_FIRST(&fl->file_list);
    else
        fl->cur = TAILQ_NEXT(fl->cur, tailq_entry);

    if (fl->cur == NULL) {
        ESP_LOGW(TAG, "current audio file node is last");
        return NULL;
    }
    ret = (struct audio_file_source *)fl->cur;
    return ret;
}

esp_err_t file_reader_read_dir(file_reader_handle_t fl, const char *dir) {
    DIR *dp = opendir(dir);
    if (dp == NULL) {
        ESP_LOGE(TAG, "open dir %s fail: %d(%s)", dir, errno, strerror(errno));
        return ESP_FAIL;
    }
    esp_err_t err = ESP_OK;

    struct dirent *dir_entry;
    sds full_path = sdsempty();
    int dir_count = 0;
    while ((dir_entry = readdir(dp)) != NULL) {
        if (dir_entry->d_name[0] == '.')
            continue; /* ignore */

        sdsclear(full_path);
        full_path = sdscatprintf(full_path, "%s/%s", dir, dir_entry->d_name);
        ESP_LOGD(TAG, "read entry: %s", full_path);
        struct stat file_stat;
        if (stat(full_path, &file_stat) != 0) {
            ESP_LOGW(TAG, "get file %s state fail: %s", full_path, strerror(errno));
            continue;
        }
        struct audio_file_source_node *node = NULL;
        bool is_dir = false;
        if (S_ISDIR(file_stat.st_mode)) {
            // dir
            ESP_LOGD(TAG, "entry %s is dir", full_path);
            node = amp_malloc(sizeof(struct audio_file_source_node));
            if (!node) {
                err = ESP_ERR_NO_MEM;
                break;
            }
            is_dir = true;
            dir_count++;
        } else {
            // file
            ESP_LOGD(TAG, "entry %s is file, mode: %d", full_path);
            const char *ext = strrchr(dir_entry->d_name, '.');
            if (ext) {
                if (strcasecmp(ext, FILE_TYPE_NAME_MP3) == 0) {
                    AUDIO_FILE_NODE_CREATE(node, err, AUDIO_MEDIA_TYPE_MP3, break;);
                } else if (strcasecmp(ext, FILE_TYPE_NAME_AAC) == 0) {
                    AUDIO_FILE_NODE_CREATE(node, err, AUDIO_MEDIA_TYPE_AAC, break;);
                } else if (strcasecmp(ext, FILE_TYPE_NAME_FLAC) == 0) {
                    AUDIO_FILE_NODE_CREATE(node, err, AUDIO_MEDIA_TYPE_FLAC, break;);
                } else {
                    ESP_LOGW(TAG, "file %s is not audio file, ignore", full_path);
                }
            } else {
                ESP_LOGW(TAG, "file %s ext is empty, ignore", full_path);
            }
        }
        if (node) {
            node->source.name = sdsdup(full_path);
            node->source.is_dir = is_dir;
            ESP_LOGI(TAG, "load file %s, is_dir: %d", full_path, is_dir);
            TAILQ_INSERT_TAIL(&fl->file_list, node, tailq_entry);
            fl->size++;
        }
    }
    if (ESP_OK == err)
        ESP_LOGI(TAG, "load all file success, total count: %d, dir count: %d", fl->size, dir_count);

    fl->base = sdscat(sdsempty(), dir);
    sdsfree(full_path);
    closedir(dp);
    return err;
}

esp_err_t file_reader_init(file_reader_handle_t *fr) {
    file_reader_handle_t f = amp_malloc(sizeof(struct file_reader));
    if (!f)
        return ESP_ERR_NO_MEM;

    f->size = 0;
    f->base = NULL;
    TAILQ_INIT(&f->file_list);
    *fr = f;
    return ESP_OK;
}

void file_reader_deinit(file_reader_handle_t fr) {
    if (!fr)
        return;
    if (fr->base)
        sdsfree(fr->base);
    struct audio_file_source_node *node;
    TAILQ_FOREACH(node, &fr->file_list, tailq_entry) {
        if (!node)
            continue;
        if (node->source.name)
            sdsfree((sds)node->source.name);
        amp_free(node);
    }
    amp_free(fr);
}
