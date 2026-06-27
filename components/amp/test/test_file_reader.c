#include "amp/file_reader.h"
#include "esp_log.h"
#include "unity.h"

static const char *TAG = "test_amp_file_reader";

TEST_CASE("File Reader init", "[amp]") {
    amp_file_reader_handle_t fr;
    esp_err_t err = amp_file_reader_init(&fr);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(fr);

    amp_file_reader_deinit(fr);
}

TEST_CASE("File Reader load all file info", "[amp][file_reader]") {
    amp_file_reader_handle_t fr;
    esp_err_t err = amp_file_reader_init(&fr);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(fr);
    err = amp_file_reader_read_dir(fr, "/storage/music");
    TEST_ASSERT_EQUAL(ESP_OK, err);

    amp_audio_file_source_t *src = NULL;
    while ((src = amp_file_reader_next(fr)) != NULL) {
        ESP_LOGI(TAG, "load file: %s, is_dir: %d", src->name, src->is_dir);
    }

    amp_file_reader_deinit(fr);
}
