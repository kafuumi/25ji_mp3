
#include "esp_log.h"
#include <math.h>

#include "amp/sin_pcm_reader.h"

static const char *TAG = "sin_pcm";

struct sin_pcm_reader {
    int max_amplitude;
    size_t frames_size;
    RingbufHandle_t rb_out;
    struct sin_pcm_audio_args *args;
};

static void generate_sin_pcm_16bit(sin_pcm_reader_handle_t *reader, int16_t *buf, float *phase) {
    const struct sin_pcm_audio_args *args = reader->args;
    const float amplitude = reader->max_amplitude;
    float tmp = 0;
    if (phase) {
        tmp = *phase;
    }
    const float pi2 = (float)2.0f * M_PI;
    for (size_t i = 0; i < reader->frames_size; ++i) {
        float delta = pi2 * args->freq / args->sample_rate;
        tmp += delta;
        if (tmp >= pi2) {
            tmp -= pi2;
        }
        int16_t value = (int16_t)lrintf(sinf(tmp) * amplitude * (args->volume / 100.));
        *buf = value;
        buf++;
        if (args->channel == PCM_CHANNEL_STERO) {
            // write right channel
            *buf = value;
            buf++;
        }
    }
    *phase = tmp;
}

static void sin_pcm_reader_task(void *args) {
    sin_pcm_reader_handle_t *reader = (sin_pcm_reader_handle_t *)args;
    size_t buf_size = reader->frames_size * reader->args->channel;
    buf_size *= sizeof(int16_t);
    void *buf = malloc(buf_size);
    float phase = 0;
    while (true) {
        generate_sin_pcm_16bit(reader, buf, &phase);
        BaseType_t ret = xRingbufferSend(reader->rb_out, buf, buf_size, portMAX_DELAY);
        if (!ret) {
            ESP_LOGE(TAG, "write data to rb fail: %d", ret);
        } else {
            ESP_LOGI(TAG, "write data to rb, dats ptr: %p", buf);
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

esp_err_t sin_pcm_reader_init(struct sin_pcm_reader_cfg *cfg, sin_pcm_reader_handle_t **reader) {
    sin_pcm_reader_handle_t *r = malloc(sizeof(sin_pcm_reader_handle_t));
    if (!r) {
        return ESP_ERR_NO_MEM;
    }
    r->frames_size = cfg->frames_size;
    r->max_amplitude = cfg->max_amplitude;
    struct sin_pcm_audio_args *args = malloc(sizeof(struct sin_pcm_audio_args));
    struct sin_pcm_audio_args a = {
        .bit_width = PCM_BIT_WIDTH_16BIT,
        .channel = PCM_CHANNEL_MONO,
        .freq = 440,
        .sample_rate = 44100,
        .volume = 80,
    };
    *args = a;
    r->args = args;
    r->rb_out = cfg->rb_out;
    *reader = r;
    return ESP_OK;
}

void sin_pcm_reader_deinit(sin_pcm_reader_handle_t *reader) {
    if (!reader) {
        return;
    }
    free(reader);
}

esp_err_t sin_pcm_reader_run(sin_pcm_reader_handle_t *reader) {
    xTaskCreate(sin_pcm_reader_task, "sin_pcm", 4096, reader, 1, NULL);
    return ESP_OK;
}