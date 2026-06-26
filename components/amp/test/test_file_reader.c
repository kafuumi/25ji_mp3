#include "amp/file_reader.h"
#include "esp_log.h"
#include "unity.h"

static const char *TAG = "test_amp_file_reader";

TEST_CASE("File Reader init", "[amp]") {
    file_reader_handle_t fr;
    esp_err_t err = file_reader_init(&fr);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(fr);

    file_reader_deinit(fr);
}

TEST_CASE("File Reader load all file info", "[amp][file_reader]") {
    file_reader_handle_t fr;
    esp_err_t err = file_reader_init(&fr);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(fr);
    err = file_reader_read_dir(fr, "/storage/music");
    TEST_ASSERT_EQUAL(ESP_OK, err);

    struct audio_file_source *src = NULL;
    while ((src = file_reader_next(fr)) != NULL) {
        ESP_LOGI(TAG, "load file: %s, is_dir: %d", src->name, src->is_dir);
    }

    file_reader_deinit(fr);
}
