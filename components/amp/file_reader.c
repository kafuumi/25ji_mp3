#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include "amp/file_reader.h"
#include "esp_log.h"

#include "sds.h"

static const char *TAG = "file_reader";

struct audio_file_source {
    sds name;
    bool is_dir;
    TAILQ_ENTRY(audio_file_source) tailq_entry;
};

typedef TAILQ_HEAD(audio_file_source_head, audio_file_source) audio_file_source_head_t;

struct audio_file_list {
    sds base;
    size_t size;
    struct audio_file_source *cur;
    audio_file_source_head_t file_list;
};

static esp_err_t audio_file_next(struct audio_file_list *fl) {
    if (fl->size == 0) {
        ESP_LOGE(TAG, "audio file list is empty");
        return ESP_ERR_NOT_FOUND;
    }
    if (fl->cur == NULL)
        fl->cur = TAILQ_FIRST(&fl->file_list);
    else
        fl->cur = TAILQ_NEXT(fl->cur, tailq_entry);

    if (fl->cur == NULL)
        ESP_LOGW(TAG, "current audio file node is last");

    return ESP_OK;
}

static esp_err_t audio_file_load(struct audio_file_list *fl, const char *dir) {
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
        if (dir_entry->d_namlen == 0 || dir_entry->d_name[0] == '.')
            continue; /* ignore */

        sdsclear(full_path);
        full_path = sdscatprintf(full_path, "%s/%s", dir, dir_entry->d_name);
        struct stat file_stat;
        if (stat(full_path, &file_stat) != 0) {
            ESP_LOGW(TAG, "get file %s state fail: %s", full_path, strerror(errno));
            continue;
        }
        struct audio_file_source *src = NULL;
        if (S_ISDIR(file_stat.st_mode)) {
            // dir
            src = malloc(sizeof(struct audio_file_source));
            if (!src) {
                err = ESP_ERR_NO_MEM;
                break;
            }
            src->name = sdsdup(full_path);
            src->is_dir = true;
            dir_count++;
        } else {
            // file
            const char *ext = strrchr(dir_entry->d_name, '.');
            if (ext && (strcmp(ext, ".mp3") == 0 || strcmp(ext, ".flac") == 0 || strcmp(ext, ".aac"))) {
                src = malloc(sizeof(struct audio_file_source));
                if (!src) {
                    err = ESP_ERR_NO_MEM;
                    break;
                }
                src->name = sdsdup(full_path);
                src->is_dir = false;
            }
        }
        if (src) {
            ESP_LOGI(TAG, "load file %s, is_dir: %d", src->name, src->is_dir);
            TAILQ_INSERT_TAIL(&fl->file_list, src, tailq_entry);
            fl->size++;
        }
    }
    if (ESP_OK == err)
        ESP_LOGI(TAG, "load all file success, total count: %d, dir count: %d", fl->size, dir_count);

    sdsfree(full_path);
    closedir(dp);
    return err;
}
