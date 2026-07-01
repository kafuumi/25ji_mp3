
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include "esp_log.h"

#include "amp/amp_mem.h"
#include "amp/playlist.h"
#include "sds.h"

#define FS_SEPARATOR "/"

#define FILE_TYPE_NAME_MP3 ".mp3"
#define FILE_TYPE_NAME_AAC ".aac"
#define FILE_TYPE_NAME_FLAC ".flac"

#define AUDIO_FILE_NODE_CREATE(var, err, type, on_fail)                                                                \
    do {                                                                                                               \
        var = amp_malloc(sizeof(struct amp_track_node_t));                                                             \
        if (!var) {                                                                                                    \
            err = ESP_ERR_NO_MEM;                                                                                      \
            break;                                                                                                     \
        }                                                                                                              \
        var->track.media_type = type;                                                                                  \
    } while (0)

static const char *TAG = "playlist";

struct amp_track_node_t {
    struct amp_track_t track;
    TAILQ_ENTRY(amp_track_node_t) tailq_entry;
};

typedef TAILQ_HEAD(amp_track_head_t, amp_track_node_t) amp_track_head_t;

struct amp_playlist_t {
    sds base;
    bool is_load;
    bool recursion;
    struct amp_track_node_t *cur;
    uint32_t track_size;
    amp_track_head_t track_list;
};

static esp_err_t amp_playlist_load(amp_playlist_handle_t playlist, const char *dir) {
    if (dir == NULL) {
        dir = playlist->base;
    }
    DIR *dp = opendir(dir);
    if (dp == NULL) {
        ESP_LOGE(TAG, "failed to open directory %s: %d(%s)", dir, errno, strerror(errno));
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
        full_path = sdscatprintf(full_path, "%s" FS_SEPARATOR "%s", dir, dir_entry->d_name);
        ESP_LOGD(TAG, "read entry: %s", full_path);
        struct stat file_stat;
        if (stat(full_path, &file_stat) != 0) {
            ESP_LOGW(TAG, "failed to stat file %s: %s", full_path, strerror(errno));
            continue;
        }
        struct amp_track_node_t *node = NULL;
        bool is_dir = false;
        if (S_ISDIR(file_stat.st_mode)) {
            // dir
            ESP_LOGD(TAG, "entry %s is dir", full_path);
            if (playlist->recursion) {
                ESP_LOGI(TAG, "recursion load %s", full_path);
                err = amp_playlist_load(playlist, full_path);
                continue;
            }
            node = amp_malloc(sizeof(struct amp_track_node_t));
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
                    ESP_LOGW(TAG, "skipping non-audio file: %s", full_path);
                }
            } else {
                ESP_LOGW(TAG, "skipping file with no extension: %s", full_path);
            }
        }
        if (node) {
            node->track.path = sdsdup(full_path);
            node->track.is_dir = is_dir;
            ESP_LOGI(TAG, "loaded entry: %s (dir=%d)", full_path, is_dir);
            TAILQ_INSERT_TAIL(&playlist->track_list, node, tailq_entry);
            playlist->track_size++;
        }
    }
    if (ESP_OK == err) {
        ESP_LOGI(TAG, "loaded %d files (%d dirs)", playlist->track_size, dir_count);
    }
    playlist->cur = TAILQ_FIRST(&playlist->track_list);
    sdsfree(full_path);
    closedir(dp);
    return err;
}

amp_track_handle_t amp_playlist_next(amp_playlist_handle_t playlist) {
    if (!playlist->is_load) {
        if (amp_playlist_load(playlist, NULL) != ESP_OK) {
            return NULL;
        }
    }
    if (playlist->cur == NULL) {
        // empty
        return NULL;
    }
    playlist->cur = TAILQ_NEXT(playlist->cur, tailq_entry);
    if (playlist->cur == NULL) {
        // end
        playlist->cur = TAILQ_FIRST(&playlist->track_list);
    }
    return &(playlist->cur->track);
}

esp_err_t amp_playlist_init(amp_playlist_cfg_t *cfg, amp_playlist_handle_t *playlist) {
    amp_playlist_handle_t p = amp_calloc(1, sizeof(struct amp_playlist_t));
    if (!p) {
        return ESP_ERR_NO_MEM;
    }
    p->base = sdscat(sdsempty(), cfg->base_dir);
    p->recursion = cfg->recursion;
    TAILQ_INIT(&p->track_list);
    *playlist = p;
    return ESP_OK;
}

void amp_playlist_deinit(amp_playlist_handle_t playlist) {
    if (!playlist) {
        return;
    }
    struct amp_track_node_t *node;
    TAILQ_FOREACH(node, &playlist->track_list, tailq_entry) {
        if (node) {
            if (node->track.path) {
                sdsfree((sds)node->track.path);
            }
            amp_free(node);
        }
    }
    if (playlist->base) {
        sdsfree(playlist->base);
    }
    amp_free(playlist);
}