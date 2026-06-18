
#include "esp_log.h"

#include "amp/i2s_writer.h"
#include "bsp.h"
#include "portmacro.h"
#include "utils/esp_utils.h"
#include <stdbool.h>

static const char *TAG = "i2s_writer";

struct i2s_writer {
    bool chan_enable;
    i2s_port_t i2s_port;
    RingbufHandle_t rb_in;
    i2s_chan_handle_t tx_chan;
};

static esp_err_t _i2s_driver_init(i2s_writer_handle_t *ctx, struct i2s_writer_output_args *args) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(ctx->i2s_port, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    i2s_chan_handle_t tx_chan = NULL;
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_chan, NULL);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "new i2s tx channel fail: " FMT_ESP_ERR(err));
        return err;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(args->sample_rate),
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
        ESP_LOGE(TAG, "init i2s channel fail: " FMT_ESP_ERR(err));
        goto cleanup;
    }
    err = i2s_channel_enable(tx_chan);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "enable i2s tx channel fail: " FMT_ESP_ERR(err));
        goto cleanup;
    }
    ctx->tx_chan = tx_chan;
    ctx->chan_enable = true;
    return ESP_OK;

cleanup:
    if (tx_chan) {
        i2s_del_channel(tx_chan);
    }

    return err;
}

static void i2s_writer_task(void *args) {
    i2s_writer_handle_t *writer = (i2s_writer_handle_t *)args;
    RingbufHandle_t rb = writer->rb_in;
    size_t data_size;
    while (true) {
        void *item = xRingbufferReceive(rb, &data_size, portMAX_DELAY);
        if (data_size == 0) {
            continue;
        }
        if (item == NULL) {
            ESP_LOGW(TAG, "no item");
            continue;
        }
        i2s_writer_send_pcm(writer, item, data_size);
        vRingbufferReturnItem(rb, item);
    }
}

// #####################################################################
// ####################### i2s_writer public ###########################
// #####################################################################

static const amp_element_interface_t i2s_amp_element_interface = {
    .task_run = i2s_writer_task,
};

esp_err_t i2s_writer_init(struct i2s_writer_cfg *cfg, i2s_writer_handle_t **writer) {
    i2s_writer_handle_t *w = malloc(sizeof(i2s_writer_handle_t));
    if (!w) {
        // no memory
        return ESP_ERR_NO_MEM;
    }
    w->chan_enable = false;
    w->tx_chan = NULL;
    w->i2s_port = cfg->i2s_port;
    w->rb_in = cfg->rb_in;

    struct i2s_writer_output_args args = AUDIO_OUTPUT_DEFAULT_ARGS();
    esp_err_t err = _i2s_driver_init(w, &args);
    if (ESP_OK != err) {
        return err;
    }
    *writer = w;
    ESP_LOGD(TAG, "initialize i2s writer success");
    return ESP_OK;
}

void i2s_writer_deinit(i2s_writer_handle_t *writer) {
    if (!writer) {
        return;
    }
    esp_err_t err = ESP_OK;
    if (writer->tx_chan) {
        if (writer->chan_enable) {
            err = i2s_channel_disable(writer->tx_chan);
        }
        if (ESP_OK != err) {
            ESP_LOGW(TAG, "disable i2s channel fail: " FMT_ESP_ERR(err));
        }
        err = i2s_del_channel(writer->tx_chan);
        if (ESP_OK != err) {
            ESP_LOGW(TAG, "delete i2s channel fail: " FMT_ESP_ERR(err));
        }
    }

    free(writer);
}

esp_err_t i2s_writer_send_pcm(i2s_writer_handle_t *writer, const uint8_t *data, size_t size) {
    size_t written = 0;
    if (writer->tx_chan == NULL || !writer->chan_enable) {
        ESP_LOGE(TAG, "i2s channel not avaliable");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_OK;
    while (written < size) {
        size_t wc = 0;
        err = i2s_channel_write(writer->tx_chan, data + written, size - written, &wc, 1000);
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

esp_err_t i2s_writer_audio_config(i2s_writer_handle_t *writer, struct i2s_writer_output_args *args) {
    i2s_chan_handle_t chan = writer->tx_chan;
    if (writer->chan_enable) {
        i2s_channel_disable(chan);
    }
    esp_err_t err = ESP_OK;
    if (args->sample_rate > 0) {
        i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(args->sample_rate);
        err = i2s_channel_reconfig_std_clock(chan, &clk_cfg);
    }
    if (args->slog_bit_width >= 0) {
        i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(args->slog_bit_width, args->slot_mode);
        err = i2s_channel_reconfig_std_slot(chan, &slot_cfg);
    }
    return ESP_OK;
}

void i2s_writer_element_deinit(void *args) { i2s_writer_deinit((i2s_writer_handle_t *)args); }

const amp_element_interface_t *i2s_writer_el_interface() {
    return &(i2s_amp_element_interface);
}
