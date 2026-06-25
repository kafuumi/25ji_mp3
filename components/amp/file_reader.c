#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <unistd.h>

#include "amp/amp_mem.h"
#include "amp/file_reader.h"
#include "element_priv.h"
#include "esp_log.h"

#include "sds.h"

#define EVENT_WAIT_TIME_MAX pdMS_TO_TICKS(100)

#define FILE_TYPE_NAME_MP3 ".mp3"
#define FILE_TYPE_NAME_AAC ".aac"
#define FILE_TYPE_NAME_FLAC ".flac"

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

static inline bool file_reader_do_event(file_reader_handle_t reader, TickType_t wait_time) {
    uint32_t notify = 0;
    if (xTaskNotifyWait(0, ULONG_MAX, &notify, wait_time) == pdTRUE) {
    }
    return amp_dashboard_is_playing(reader->el_entry.dashboard);
}

static void _send_eos_event(file_reader_handle_t reader) {
    amp_dashboard_handle_t dash = reader->el_entry.dashboard;
    amp_dashboard_swap_status(dash, AMP_STATE_WAITING_NEXT);
    amp_dashboard_send_done(dash);
}

static void file_reader_task_run(void *args) {
    file_reader_handle_t reader = args;
    ringbuf_handle_t rb = reader->rb;
    assert(rb);

    size_t buf_size = 1024;
    uint8_t *buf = amp_malloc(sizeof(uint8_t) * buf_size);
    TickType_t event_wait = EVENT_WAIT_TIME_MAX;
    TickType_t wait_time = pdMS_TO_TICKS(1000);

    int fd = 0;
    const struct audio_file_source *cur_file = NULL;
    while (true) {
        /* do event */
        if (!file_reader_do_event(reader, event_wait)) {
            event_wait = EVENT_WAIT_TIME_MAX;
            continue;
        } else if (event_wait > 0) {
            event_wait = 0;
        }
        /* open file */
        if (fd <= 0) {
            cur_file = file_reader_next(reader);
            if (cur_file == NULL || cur_file->is_dir) {
                continue;
            }
            const char *name = cur_file->name;
            fd = open(name, O_RDONLY);
            if (fd < 0) {
                ESP_LOGE(TAG, "open file %s fail: %d(%s)", name, errno, strerror(errno));
                continue;
            }
            ESP_LOGI(TAG, "open file %s success, fd: %d", name, fd);
            amp_dashboard_handle_t dash = reader->el_entry.dashboard;
            dash->audio.name = name;
            dash->audio.media_type = cur_file->media_type;
        }
        /* read data from file */
        ssize_t read_size = read(fd, buf, buf_size);
        if (read_size < 0) {
            // read error
            ESP_LOGE(TAG, "read file %s fail: %d(%s)", cur_file->name, errno, strerror(errno));
            continue;
        } else if (read_size == 0) {
            // read finished
            ESP_LOGI(TAG, "read file %s EOF", cur_file->name);
            rb_done_write(rb);
            cur_file = NULL;
            close(fd);
            fd = 0;
            _send_eos_event(reader);
            continue;
        }
        ESP_LOGD(TAG, "read file %s success, size: %d", cur_file->name, read_size);
        /* send data to ringbuf */
        int write_size = rb_write(rb, (char *)buf, read_size, wait_time);
        if (write_size < 0) {
            ESP_LOGE(TAG, "send data to ringbuf fail: %d", write_size);
            continue;
        }
    }
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
            ESP_LOGD(TAG, "entry %s is file", full_path);
            const char *ext = strrchr(dir_entry->d_name, '.');
            if (ext) {
                if (strcasecmp(ext, FILE_TYPE_NAME_MP3)) {
                    AUDIO_FILE_NODE_CREATE(node, err, AUDIO_MEDIA_TYPE_MP3, break;);
                } else if (strcasecmp(ext, FILE_TYPE_NAME_AAC)) {
                    AUDIO_FILE_NODE_CREATE(node, err, AUDIO_MEDIA_TYPE_AAC, break;);
                } else if (strcasecmp(ext, FILE_TYPE_NAME_FLAC)) {
                    AUDIO_FILE_NODE_CREATE(node, err, AUDIO_MEDIA_TYPE_FLAC, break;);
                }
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
}
