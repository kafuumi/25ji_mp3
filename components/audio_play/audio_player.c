#include "bsp.h"
#include "driver/gpio.h"
#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "driver/i2s_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "audio_player.h"

#define DATA_WRITE_TIMEOUT 1000

#define AO_DEFAULT_OUTPUT_ARGS()                                                                                       \
    {                                                                                                                  \
        .clk_rate = 8000,                                                                                              \
        .port_id = I2S_NUM_0,                                                                                          \
        .slog_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,                                                                    \
        .slot_mode = I2S_SLOT_MODE_STEREO,                                                                             \
    }

struct audio_state_machine {
    uint8_t state;
    bool tx_enable;
    i2s_chan_handle_t tx;
};

static struct audio_state_machine *audio_machine_ctx = NULL;
static const char *TAG = "audio_player";

struct audio_output_args {
    i2s_port_t port_id;
    uint32_t clk_rate;
    i2s_slot_bit_width_t slog_bit_width;
    i2s_slot_mode_t slot_mode;
};

static esp_err_t _i2s_driver_reconfig(struct audio_state_machine *ctx, struct audio_output_args *args) {
    i2s_chan_handle_t chan = ctx->tx;
    if (ctx->tx_enable) {
        i2s_channel_disable(chan);
    }
    esp_err_t err = ESP_OK;
    if (args->clk_rate > 0) {
        i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(args->clk_rate);
        err = i2s_channel_reconfig_std_clock(chan, &clk_cfg);
    }
    if (args->slog_bit_width >= 0) {
        i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(args->slog_bit_width, args->slot_mode);
        err = i2s_channel_reconfig_std_slot(chan, &slot_cfg);
    }
    return ESP_OK;
}

static esp_err_t _i2s_driver_init(struct audio_state_machine *ctx, struct audio_output_args *args) {
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
    ctx->tx = tx_chan;
    ctx->tx_enable = true;
    return ESP_OK;

cleanup:
    if (tx_chan) {
        i2s_del_channel(tx_chan);
    }

    return err;
}

static esp_err_t ao_write_pcm(i2s_chan_handle_t tx_chan, const void *data, size_t size) {
    size_t written = 0;
    esp_err_t err = ESP_OK;
    while (written < size) {
        size_t wc = 0;
        err = i2s_channel_write(tx_chan, data + written, size - written, &wc, 1000);
        if (ESP_OK != err) {
            break;
        }
        if (wc == 0) {
            break;
        }
        written += wc;
    }
    if (ESP_OK != err) {
        ESP_LOGW(TAG, "write pcm data fail: %d(%s)", err, esp_err_to_name(err));
        return err;
    }
    if (written != size) {
        ESP_LOGW(TAG, "write pcm not finished, written: %zu, total: %zu", written, size);
        return ESP_ERR_NOT_FINISHED;
    }
    return ESP_OK;
}

static inline esp_err_t ao_set_mute(bool mute) {
    if (mute) {
        return gpio_set_level(BSP_PIN_I2S_MUTE, 0); // mute
    }
    return gpio_set_level(BSP_PIN_I2S_MUTE, 1); // unmute
}

static inline esp_err_t ao_config() {
    audio_machine_ctx = malloc(sizeof(struct audio_state_machine));
    if (audio_machine_ctx == NULL) {
        // no memory
        return ESP_ERR_NO_MEM;
    }
    // enable i2s tx channel
    struct audio_output_args args = AO_DEFAULT_OUTPUT_ARGS();
    return _i2s_driver_init(audio_machine_ctx, &args);
}

esp_err_t ao_init() {
    esp_err_t err = ao_config();
    if (ESP_OK != err) {
        return err;
    }
    gpio_set_direction(BSP_PIN_I2S_MUTE, GPIO_MODE_OUTPUT);
    ao_set_mute(true); // mute
    return ESP_OK;
}

void ao_deinit() {
    if (!audio_machine_ctx) {
        return;
    }
    if (audio_machine_ctx->tx) {
        if (audio_machine_ctx->tx_enable) {
            i2s_channel_disable(audio_machine_ctx->tx);
        }
        i2s_del_channel(audio_machine_ctx->tx);
    }
    audio_machine_ctx->tx = NULL;
    audio_machine_ctx->tx_enable = false;
    free(audio_machine_ctx);
}

struct mock_pcm_args {
    uint8_t volume;         // 音量
    uint8_t channel;        // 声道数
    int freq;               // 频率
    uint16_t samples_rate;  // 采样率
    uint16_t max_amplitude; // 最大振幅
};

// sine ware, signed 16bit
void _generate_sin_pcm(int16_t *buf, size_t frames, struct mock_pcm_args *args, float *phase) {
    const float amplitude = args->max_amplitude;
    if (phase == NULL) {
        float tmp = 0;
        phase = &tmp;
    }
    float pi2 = (float)2.0f * M_PI;
    for (size_t i = 0; i < frames; i++) {
        float delta = pi2 * args->freq / args->samples_rate;
        *phase += delta;
        if (*phase >= pi2) {
            *phase -= pi2;
        }
        float value = sinf(*phase);
        int16_t av = (int16_t)lrintf(value * amplitude * ((float)args->volume / 100.0));
        for (int j = 0; j < args->channel; j++) {
            *buf = av;
            buf++;
        }
    }
}

static void ao_sin_test_task(void *_user_data) {
    float phase = 0.0;
    const size_t frames = 2048;
    const int samples_rate = 48000; // 44.1 khz
    const int freq = 440;           // 440 hz
    const int channel = 2;
    const size_t buf_size = sizeof(int16_t) * frames * channel;
    int16_t *buf = malloc(buf_size);
    memset(buf, 0, frames * channel * sizeof(int16_t));
    const double duration = (float)frames / samples_rate * 1000; // ms
    double times = 0;
    struct mock_pcm_args args = {
        .channel = channel,
        .freq = freq,
        .samples_rate = samples_rate,
        .max_amplitude = 4000,
        .volume = 50,
    };
    while (times <= 10000) {
        _generate_sin_pcm(buf, frames, &args, &phase);
        ao_write_pcm(audio_machine_ctx->tx, buf, buf_size);
        times += duration;
    }
    free(buf);
}

void ao_sin_test() { xTaskCreate(ao_sin_test_task, "ao_sin_test", 10240, NULL, 5, NULL); }