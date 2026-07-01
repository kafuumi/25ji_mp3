#include "amp/playlist.h"
#include "esp_log.h"
#include "unity.h"

#define TEST_MOUNT_DIR "/storage"
#define TEST_DIR TEST_MOUNT_DIR "/music"

static const char *TAG = "test_amp_file_reader";

static amp_playlist_handle_t playlist_init(const char *base, bool recursion) {
    amp_playlist_handle_t playlist;
    if (base == NULL) {
        base = TEST_DIR;
    }
    amp_playlist_cfg_t cfg = {
        .base_dir = base,
        .recursion = recursion,
    };
    esp_err_t err = amp_playlist_init(&cfg, &playlist);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(playlist);
    return playlist;
}

TEST_CASE("playlist init and deinit", "[amp][playlist]") {
    amp_playlist_handle_t playlist = playlist_init(NULL, false);
    amp_playlist_deinit(playlist);
}

TEST_CASE("playlist load next", "[amp][playlist]") {
    amp_playlist_handle_t playlist = playlist_init(NULL, false);
    amp_track_handle_t track = amp_playlist_next(playlist);
    TEST_ASSERT_NOT_NULL(track);
    amp_playlist_deinit(playlist);
}

TEST_CASE("playlist load next with recursion", "[amp][playlist]") {
    amp_playlist_handle_t playlist = playlist_init(TEST_MOUNT_DIR, true);
    amp_track_handle_t track = amp_playlist_next(playlist);
    TEST_ASSERT_NOT_NULL(track);
    amp_playlist_deinit(playlist);
}
