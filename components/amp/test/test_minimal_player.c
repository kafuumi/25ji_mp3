
#include "unity.h"

#include "amp/audio_decoder.h"
#include "amp/controller.h"
#include "amp/devnull_writer.h"
#include "amp/file_reader.h"
#include "amp/i2s_writer.h"
#include "bsp.h"
#include "esp_log.h"

static const char *TAG = "minimal_player";

static void print_heap(const char *tag) {
    ESP_LOGW(tag, "free=%u min_free=%u largest=%u", heap_caps_get_free_size(MALLOC_CAP_8BIT),
             heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

TEST_CASE("Minimal Player", "[amp]") {
    // amp_devnull_writer_handle_t null_writer;
    // esp_err_t err = amp_devnull_writer_init(&null_writer);
    // TEST_ASSERT_EQUAL(ESP_OK, err);
    bsp_init();
    bsp_audio_mute(false);
    amp_i2s_writer_handle_t writer;
    amp_i2s_writer_cfg_t i2s_cfg = {
        .i2s_port = I2S_NUM_0,
    };
    esp_err_t err = amp_i2s_writer_init(&i2s_cfg, &writer);

    amp_audio_decoder_handle_t codec;
    err = amp_audio_decoder_init(&codec);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    amp_file_reader_handle_t file_reader;
    amp_file_reader_cfg_t reader_cfg = {
        .playlist = NULL,
    };
    err = amp_file_reader_init(&reader_cfg, &file_reader);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    // amp_file_reader_read_dir(file_reader, "/storage/music");

    amp_controller_handle_t controller;
    err = amp_controller_init(&controller);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    amp_element_task_config_t task_cfg = {
        .name = "reader",
        .output_rb_size = 1024,
        .stack_size = 4096,
        .intf = amp_file_reader_get_element_interface(),
    };
    amp_controller_append_reader(controller, (amp_element_handle_t)file_reader, &task_cfg);
    task_cfg.name = "audio_codec";
    task_cfg.intf = amp_audio_decoder_get_element_interface();
    amp_controller_append_processor(controller, (amp_element_handle_t)codec, &task_cfg);
    task_cfg.name = "null_writer";
    task_cfg.intf = amp_i2s_writer_get_element_interface();
    amp_controller_append_writer(controller, (amp_element_handle_t)writer, &task_cfg);

    amp_controller_run(controller);
    amp_controller_action_play(controller);
    while (true) {
        print_heap(TAG);
        vTaskDelay(pdMS_TO_TICKS(10 * 1000));
    }
}
