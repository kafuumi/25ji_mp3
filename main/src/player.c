
#include "src/player.h"
#include "amp/controller.h"
#include "amp/i2s_writer.h"
#include "amp/sin_pcm_reader.h"
#include "driver/i2s_types.h"
#include "esp_err.h"
#include "freertos/ringbuf.h"

void audio_test() {
    RingbufHandle_t rb = xRingbufferCreate(4096, RINGBUF_TYPE_BYTEBUF);
    i2s_writer_handle_t *i2s_writer;
    struct i2s_writer_cfg cfg = {
        .i2s_port = I2S_NUM_0,
        .rb_in = rb,
    };
    esp_err_t err = i2s_writer_init(&cfg, &i2s_writer);
    ESP_ERROR_CHECK(err);

    sin_pcm_reader_handle_t *pcm_reader;
    struct sin_pcm_reader_cfg pcm_cfg = {
        .frames_size = 1024,
        .max_amplitude = 3000,
        .rb_out = rb,
    };
    err = sin_pcm_reader_init(&pcm_cfg, &pcm_reader);
    ESP_ERROR_CHECK(err);

    struct sin_pcm_audio_args audio_args = {
        .bit_width = PCM_BIT_WIDTH_16BIT,
        .channel = PCM_CHANNEL_STERO,
        .freq = 740,
        .sample_rate = 44100,
        .volume = 60,
    };
    sin_pcm_config_audio(pcm_reader, &audio_args);

    amp_controller_handle_t *controller;
    err = amp_controller_init(&controller);
    ESP_ERROR_CHECK(err);

    struct amp_element_task_cfg el_cfg = {
        .name = "i2s_writer",
        .stack_size = 4096,
    };
    amp_element_handle_t *el_writer = amp_element_setup(i2s_writer, i2s_writer_el_interface(), &el_cfg);
    el_cfg.name = "pcm_reader";
    amp_element_handle_t *el_reader = amp_element_setup(pcm_reader, sin_pcm_reader_el_interface(), &el_cfg);

    amp_controller_append(controller, el_reader, NULL);
    amp_controller_append(controller, el_writer, rb);

    amp_controller_run(controller);
}
