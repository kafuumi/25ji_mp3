#if !defined(_AMP_PLAYLIST_H_)
#define _AMP_PLAYLIST_H_

#include <stdbool.h>

#include "esp_err.h"

#include "amp/audio_types.h"

struct amp_track_t {
    const char *path;
    const char *name;
    bool is_dir;
    enum amp_audio_media_type media_type;
};

typedef struct amp_track_t *amp_track_handle_t;

typedef struct {
    const char *base_dir;
    bool recursion;
} amp_playlist_cfg_t;

typedef struct amp_playlist_t *amp_playlist_handle_t;

esp_err_t amp_playlist_preload(amp_playlist_handle_t playlist);

amp_track_handle_t amp_playlist_get_current(amp_playlist_handle_t playlist);

amp_track_handle_t amp_playlist_prev(amp_playlist_handle_t);

amp_track_handle_t amp_playlist_next(amp_playlist_handle_t);

esp_err_t amp_playlist_init(amp_playlist_cfg_t *, amp_playlist_handle_t *);

void amp_playlist_deinit(amp_playlist_handle_t);

#endif // _AMP_PLAYLIST_H_
