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

static void file_reader_task_run(void *args) {
    file_reader_handle_t reader = args;
    ringbuf_handle_t rb = reader->rb;
    assert(rb);

    size_t buf_size = 1024;
    uint8_t *buf = amp_malloc(sizeof(uint8_t) * buf_size);
    TickType_t wait_time = pdMS_TO_TICKS(1000);

    int fd = 0;
    const char *cur_file = NULL;
    while (true) {
        if (fd <= 0) {
            struct audio_file_source *af = file_reader_next(reader);
            if (af == NULL || af->is_dir) {
                continue;
            }
            cur_file = af->name;
            fd = open(cur_file, O_RDONLY);
            if (fd < 0) {
                ESP_LOGE(TAG, "open file %s fail: %d(%s)", cur_file, errno, strerror(errno));
                continue;
            }
            ESP_LOGI(TAG, "open file %s success, fd: %d", cur_file, fd);
        }
        ssize_t read_size = read(fd, buf, buf_size);
        if (read_size < 0) {
            // read error
            ESP_LOGE(TAG, "read file %s fail: %d(%s)", cur_file, errno, strerror(errno));
            continue;
        } else if (read_size == 0) {
            // read finished
            ESP_LOGI(TAG, "read file %s EOF", cur_file);
            cur_file = NULL;
            close(fd);
            fd = 0;
            continue;
        }
        ESP_LOGD(TAG, "read file %s success, size: %d", cur_file, read_size);
        // send to ringbuf
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
            if (ext && (strcmp(ext, ".mp3") == 0 || strcmp(ext, ".flac") == 0 || strcmp(ext, ".aac"))) {
                node = amp_malloc(sizeof(struct audio_file_source_node));
                if (!node) {
                    err = ESP_ERR_NO_MEM;
                    break;
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
