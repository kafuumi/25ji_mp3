#include "unity.h"

#include "amp/controller.h"
#include "amp/i2s_writer.h"
#include "amp/sin_pcm_reader.h"

#define ELEMENT_CREATE(init_func, obj)                                                                                 \
    {                                                                                                                  \
        init_func;                                                                                                     \
        TEST_ASSERT_EQUAL(ESP_OK, err);                                                                                \
        TEST_ASSERT_NOT_NULL(obj);                                                                                     \
    }

static amp_sine_pcm_reader_handle_t create_sin_pcm_reader() {
    amp_sine_pcm_reader_handle_t reader;
    amp_sine_pcm_reader_cfg_t cfg = {
        .frames_size = 512,
        .max_amplitude = 3000,
    };
    esp_err_t err;
    ELEMENT_CREATE(err = amp_sine_pcm_reader_init(&cfg, &reader);, reader);

    amp_sine_pcm_audio_config_t audio_cfg = {
        .bit_width = AUDIO_BIT_WIDTH_16BIT,
        .channel = AUDIO_CHANNEL_STEREO,
        .freq = 440,
        .sample_rate = 44100,
        .volume = 100,
    };
    amp_sine_pcm_reader_set_audio_config(reader, &audio_cfg);
    return reader;
}

static amp_i2s_writer_handle_t create_i2s_writer(uint8_t volume) {
    amp_i2s_writer_handle_t writer;
    amp_i2s_writer_cfg_t cfg = {
        .i2s_port = I2S_NUM_0,
        .volume = volume,
    };
    esp_err_t err;
    ELEMENT_CREATE(err = amp_i2s_writer_init(&cfg, &writer), writer);
    return writer;
}

static amp_controller_handle_t create_controller() {
    amp_controller_handle_t controller;
    esp_err_t err;
    ELEMENT_CREATE(err = amp_controller_init(&controller), controller);
    return controller;
}

TEST_CASE("volume change to 50", "[amp][i2s_writer]") {
    amp_sine_pcm_reader_handle_t reader = create_sin_pcm_reader();
    amp_i2s_writer_handle_t writer = create_i2s_writer(50);
    amp_controller_handle_t controller = create_controller();

    amp_element_task_config_t reader_task_cfg = {
        .intf = amp_sine_pcm_reader_get_element_interface(),
        .name = "reader",
        .output_rb_size = 2048,
        .stack_size = 4096,
    };
    amp_controller_append_reader(controller, (amp_element_handle_t)reader, &reader_task_cfg);

    amp_element_task_config_t writer_task_cfg = {
        .intf = amp_i2s_writer_get_element_interface(),
        .name = "writer",
        .output_rb_size = 1024,
        .stack_size = 4096,
    };
    amp_controller_append_writer(controller, (amp_element_handle_t)writer, &writer_task_cfg);

    amp_controller_run(controller);
    amp_controller_action_play(controller);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}