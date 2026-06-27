
#include "unity.h"

#include "amp/audio_codec.h"
#include "amp/controller.h"
#include "amp/devnull_writer.h"
#include "amp/file_reader.h"
#include "amp/i2s_writer.h"
#include "bsp.h"

TEST_CASE("Minimal Player", "[amp]") {
    // devnull_writer_handle_t null_writer;
    // esp_err_t err = devnull_writer_init(&null_writer);
    // TEST_ASSERT_EQUAL(ESP_OK, err);
    bsp_init();
    bsp_audio_mute(false);
    i2s_writer_handle_t writer;
    struct i2s_writer_cfg i2s_cfg = {
        .i2s_port = I2S_NUM_0,
    };
    esp_err_t err = i2s_writer_init(&i2s_cfg, &writer);

    audio_codec_handle_t codec;
    err = audio_codec_init(&codec);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    file_reader_handle_t file_reader;
    err = file_reader_init(&file_reader);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    file_reader_read_dir(file_reader, "/storage/music");

    amp_controller_handle_t controller;
    err = amp_controller_init(&controller);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    struct amp_element_task_cfg task_cfg = {
        .name = "reader",
        .rb_out_size = 1024,
        .stack_size = 4096,
    };
    amp_controller_append_reader(controller, (amp_element_handle_t)file_reader, file_reader_el_interface(), &task_cfg);
    task_cfg.name = "audio_codec";
    amp_controller_append_processor(controller, (amp_element_handle_t)codec, audio_codec_el_interface(), &task_cfg);
    task_cfg.name = "null_writer";
    amp_controller_append_writer(controller, (amp_element_handle_t)writer, i2s_writer_el_interface(), &task_cfg);

    amp_controller_run(controller);
    amp_controller_action_play(controller);
    while (true)
        vTaskDelay(pdMS_TO_TICKS(10 * 1000));
}
