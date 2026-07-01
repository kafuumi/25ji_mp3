
#include "src/player.h"
#include "amp/controller.h"
#include "amp/i2s_writer.h"
#include "amp/sin_pcm_reader.h"
#include "driver/i2s_types.h"
#include "esp_err.h"

void audio_test() {
    amp_i2s_writer_handle_t i2s_writer;
    amp_i2s_writer_cfg_t cfg = {
        .i2s_port = I2S_NUM_0,
    };
    esp_err_t err = amp_i2s_writer_init(&cfg, &i2s_writer);
    ESP_ERROR_CHECK(err);

    amp_sine_pcm_reader_handle_t pcm_reader;
    amp_sine_pcm_reader_cfg_t pcm_cfg = {
        .frames_size = 128,
        .max_amplitude = 3000,
    };
    err = amp_sine_pcm_reader_init(&pcm_cfg, &pcm_reader);
    ESP_ERROR_CHECK(err);

    amp_sine_pcm_audio_config_t audio_args = {
        .bit_width = AUDIO_BIT_WIDTH_16BIT,
        .channel = AUDIO_CHANNEL_STEREO,
        .freq = 440,
        .sample_rate = 44100,
        .volume = 60,
    };
    amp_sine_pcm_reader_set_audio_config(pcm_reader, &audio_args);

    amp_controller_handle_t controller;
    err = amp_controller_init(&controller);
    ESP_ERROR_CHECK(err);

    amp_element_task_config_t el_cfg = {
        .name = "sin_pcm",
        .stack_size = 4096,
        .output_rb_size = 1024,
        .intf = amp_sine_pcm_reader_get_element_interface(),
    };

    amp_controller_append_reader(controller, (amp_element_handle_t )pcm_reader, &el_cfg);
    el_cfg.name = "i2s_writer";
    el_cfg.intf = amp_i2s_writer_get_element_interface();
    amp_controller_append_writer(controller, (amp_element_handle_t )i2s_writer, &el_cfg);

    amp_controller_run(controller);

    vTaskDelay(pdMS_TO_TICKS(3 * 1000));
    amp_controller_action_play(controller);

    vTaskDelay(pdMS_TO_TICKS(10 * 1000));
    amp_controller_action_pause(controller);
}
