#include "bsp.h"
#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "driver/i2s_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "hal/i2s_types.h"
#include "soc/gpio_num.h"

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
    return ESP_OK;

cleanup:
    if (tx_chan) {
        i2s_del_channel(tx_chan);
    }

    return err;
}

esp_err_t ao_write(uint8_t *data, int size) {
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
    return err;
}

esp_err_t ao_init() {
    audio_ctx = malloc(sizeof(struct audio_state));
    if (audio_ctx == NULL) {
        // no memory
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ao_deinit() {
    if (!audio_ctx) {
        return;
    }
    free(audio_ctx);
}
