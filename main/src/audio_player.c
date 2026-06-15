#include "bsp.h"
#include "driver/gpio.h"
#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "driver/i2s_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "hal/gpio_types.h"
#include "hal/i2s_types.h"
#include "soc/gpio_num.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "audio_player.h"

#define DATA_WRITE_TIMEOUT 1000

struct audio_state {
    uint8_t state;
    i2s_chan_handle_t tx;
};

static struct audio_state *audio_ctx = NULL;
static const char *TAG = "audio_player";

typedef struct {
    i2s_port_t port_id;
    uint32_t clk_rate;
    i2s_slot_bit_width_t slog_bit_width;
    i2s_slot_mode_t slot_mode;
} audio_output_args;

esp_err_t i2s_driver_init(audio_output_args *args) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(args->port_id, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    i2s_chan_handle_t tx_chan = NULL;
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_chan, NULL);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "new i2s tx channel fail: %d(%s)", err, esp_err_to_name(err));
        return err;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(args->clk_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(args->slog_bit_width, args->slot_mode),
        .gpio_cfg =
            {
                .bclk = BSP_PIN_I2S_BCK,
                .mclk = BSP_PIN_I2S_MCK,
                .dout = BSP_PIN_I2S_DOUT,
                .din = GPIO_NUM_NC,
                .ws = BSP_PIN_I2S_WS,
            },
    };
    err = i2s_channel_init_std_mode(tx_chan, &std_cfg);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "init i2s channel fail: %d(%s)", err, esp_err_to_name(err));
        goto cleanup;
    }
    err = i2s_channel_enable(tx_chan);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "enable i2s tx channel fail: %d(%s)", err, esp_err_to_name(err));
        goto cleanup;
    }
    audio_ctx->tx = tx_chan;
    return ESP_OK;

cleanup:
    if (tx_chan) {
        i2s_del_channel(tx_chan);
    }

    return err;
}

esp_err_t ao_write_pcm(const uint8_t *data, int size) {
    size_t written = 0;
    i2s_chan_handle_t tx = audio_ctx->tx;
    esp_err_t err = ESP_OK;
    while (written < size) {
        size_t wc = 0;
        err = i2s_channel_write(tx, data + written, size - written, &wc, 1000);
        if (ESP_OK != err) {
            break;
        }
        if (wc == 0) {
            break;
        }
        written += wc;
    }
    if (ESP_OK != err) {
        ESP_LOGW(TAG, "write data fail: %s", esp_err_to_name(err));
    }
    if (written != size) {
        ESP_LOGW(TAG, "write fail written: %zu, size: %d", written, size);
    }
    return err;
}

esp_err_t ao_init() {
    audio_ctx = malloc(sizeof(struct audio_state));
    if (audio_ctx == NULL) {
        // no memory
        return ESP_ERR_NO_MEM;
    }
    audio_output_args args = {
        .clk_rate = 48000,
        .port_id = I2S_NUM_0,
        .slog_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
        .slot_mode = I2S_SLOT_MODE_STEREO,
    };
    gpio_set_direction(BSP_PIN_I2S_MUTE, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_PIN_I2S_MUTE, 1); // unmute
    return i2s_driver_init(&args);
}

void ao_deinit() {
    if (!audio_ctx) {
        return;
    }
    free(audio_ctx);
}

void _generate_sin_s16_pcm(int16_t *buf, size_t frames, int channel, int freq, int samples_rate, float *phase) {
    const float amplitude = 8000.0f;
    if (phase == NULL) {
        float tmp = 0;
        phase = &tmp;
    }
    float two_pi = (float)2.0f * M_PI;
    for (size_t i = 0; i < frames; i++) {
        float delta = two_pi * freq / samples_rate;
        *phase += delta;
        if (*phase >= two_pi) {
            *phase -= two_pi;
        }
        float value = sinf(*phase);
        int16_t av = (int16_t)(value * amplitude);
        for (int j = 0; j < channel; j++) {
            *buf = av;
            buf++;
        }
    }
}

static void ao_sin_test_task(void *args) {
    float phase = 0.0;
    const size_t frames = 2048;
    const int samples_rate = 48000; // 44.1 khz
    const int freq = 440;           // 440 hz
    const int channel = 2;
    int16_t *buf = malloc(sizeof(int16_t) * frames * channel);
    memset(buf, 0, frames * channel * sizeof(int16_t));
    while (true) {
        int16_t t0 = esp_timer_get_time();
        _generate_sin_s16_pcm(buf, frames, channel, freq, samples_rate, &phase);
        int16_t t1 = esp_timer_get_time();
        ESP_LOGI(TAG, "time cost: %lld us", t1 - t0);
        ao_write_pcm((const uint8_t *)buf, channel * frames * sizeof(int16_t));
    }
    free(buf);
}

void ao_sin_test() { xTaskCreate(ao_sin_test_task, "ao_sin_test", 10240, NULL, 5, NULL); }
