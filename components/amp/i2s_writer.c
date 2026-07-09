#include <math.h>

#include "amp/controller.h"
#include "esp_check.h"
#include "esp_log.h"

#include "amp/amp_event.h"
#include "amp/amp_mem.h"
#include "amp/i2s_writer.h"
#include "element_priv.h"
#include "utils/esp_utils.h"

#define AMP_I2S_WRITER_EVENT_WAIT_TICKS pdMS_TO_TICKS(100)
#define AMP_I2S_WRITER_READ_WAIT_TICKS pdMS_TO_TICKS(1000)
#define AMP_I2S_WRITER_WRITE_RETRY_COUNT 3

#define I2S_DEFAULT_STD_CLK (44100)
#define I2S_DEFAULT_BIT_WIDTH I2S_DATA_BIT_WIDTH_16BIT
#define I2S_DEFAULT_SLOT_MODE I2S_SLOT_MODE_STEREO

static const char *TAG = "i2s_writer";
int32_t volume_table[99];

struct i2s_writer {
    AMP_ELEMENT_ENTRY() el_entry;
    bool chan_enable;
    i2s_port_t i2s_port;
    ringbuf_handle_t rb_in;
    i2s_chan_handle_t tx_chan;
    int32_t volume_gain;
    enum amp_audio_bit_width bit_width;
};

typedef enum {
    IW_STATE_PLAYING,
    IW_STATE_WAIT_NOTIFY,
} amp_i2s_writer_state_t;

struct amp_i2s_writer_task_state {
    TickType_t event_wait_ticks;
    bool stopped;
    bool new_stream;
    amp_i2s_writer_state_t state;
};

/*
 * ############################################################
 * ########################## private #########################
 * ############################################################
 */

#define VOLUME_CHANGE_RANGE 50
#define VOLUME_Q_FORMAT 14
#define _INT24_MAX 0x7FFFFF
#define _INT24_MIN (-0x800000)

#define _APPLY_VOLUME(type, data, size, gain)                                                                          \
    {                                                                                                                  \
        type *_data = (type *)data;                                                                                    \
        for (size_t i = 0; i < size; ++i) {                                                                            \
            _data[i] = (type)(((typeof(gain))_data[i] * gain) >> VOLUME_Q_FORMAT);                                     \
        }                                                                                                              \
    }

static void amp_i2s_writer_setup_volume_table() {
    for (int i = 0; i < 99; i++) {
        int range = VOLUME_CHANGE_RANGE; // (-60 dB, 0 dB)
        float y = powf((float)i / 100.0, 2.0);
        // gain = pow(10, dB/20)
        int32_t gain = lrintf(powf(10.0, (-range + y * range) / 20.0) * (1 << VOLUME_Q_FORMAT));
        volume_table[i] = gain;
    }
}

static inline void amp_i2s_writer_apply_volume(amp_i2s_writer_handle_t writer, void *data, size_t size) {
    int32_t gain = writer->volume_gain;
    if (gain == 1 << VOLUME_Q_FORMAT) {
        return;
    } else if (gain == 0) {
        memset((void *)data, 0, size);
        return;
    }

    switch (writer->bit_width) {
    case AUDIO_BIT_WIDTH_8BIT:
        _APPLY_VOLUME(int8_t, data, size, gain);
        break;
    case AUDIO_BIT_WIDTH_16BIT:
        _APPLY_VOLUME(int16_t, data, size >> 1, gain);
        break;
    case AUDIO_BIT_WIDTH_24BIT:
        do {
            uint8_t *_data = (uint8_t *)data;
            for (size_t i = 0; (i + 2) < size; i += 3) {
                int32_t raw = (_data[i]) | (_data[i + 1] << 8) | (_data[i + 2] << 16);
                if (raw & 0x00800000) {
                    raw |= ~0x00FFFFFF;
                }
                raw = (int32_t)(((int64_t)raw * gain) >> VOLUME_Q_FORMAT);
                if (raw > _INT24_MAX) {
                    raw = _INT24_MAX;
                } else if (raw < _INT24_MIN) {
                    raw = _INT24_MIN;
                }
                _data[i] = (uint8_t)(raw & 0xFF);
                _data[i + 1] = (uint8_t)((raw >> 8) & 0xFF);
                _data[i + 2] = (uint8_t)((raw >> 16) & 0xFF);
            }
        } while (0);
        break;
    case AUDIO_BIT_WIDTH_32BIT:
        do {
            int64_t vol = gain;
            _APPLY_VOLUME(int32_t, data, size >> 2, vol);
        } while (0);
        break;
    }
}

static esp_err_t amp_i2s_writer_write_pcm(amp_i2s_writer_handle_t writer, void *data, size_t size) {
    size_t written = 0;
    if (writer->tx_chan == NULL || !writer->chan_enable) {
        ESP_LOGE(TAG, "I2S channel not available");
        return ESP_ERR_INVALID_STATE;
    }
    amp_i2s_writer_apply_volume(writer, data, size);
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
        ESP_LOGW(TAG, "PCM write failed: %d(%s)", err, esp_err_to_name(err));
        return err;
    }
    if (written != size) {
        ESP_LOGW(TAG, "incomplete PCM write: %zu/%zu bytes", written, size);
        return ESP_ERR_NOT_FINISHED;
    }
    return ESP_OK;
}

static esp_err_t amp_i2s_writer_driver_init(amp_i2s_writer_handle_t ctx, amp_i2s_writer_gpio_cfg_t *gpio_cfg) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(ctx->i2s_port, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    i2s_chan_handle_t tx_chan = NULL;
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_chan, NULL);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "new i2s tx channel fail: " FMT_ESP_ERR(err));
        return err;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_DEFAULT_STD_CLK),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DEFAULT_BIT_WIDTH, I2S_DEFAULT_SLOT_MODE),
        .gpio_cfg =
            {
                .bclk = gpio_cfg->bclk,
                .mclk = gpio_cfg->mclk,
                .dout = gpio_cfg->dout,
                .din = GPIO_NUM_NC,
                .ws = gpio_cfg->ws,
            },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_768;
    err = i2s_channel_init_std_mode(tx_chan, &std_cfg);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "init i2s channel fail: " FMT_ESP_ERR(err));
        goto cleanup;
    }

    ctx->tx_chan = tx_chan;
    ctx->chan_enable = false;
    return ESP_OK;

cleanup:
    if (tx_chan) {
        i2s_del_channel(tx_chan);
    }

    return err;
}

static esp_err_t amp_i2s_writer_config_output_slot(amp_i2s_writer_handle_t writer) {
    struct amp_audio_detail detail;
    esp_err_t err =
        amp_dashboard_load_audio_detail(writer->el_entry.dashboard, &detail, AMP_I2S_WRITER_READ_WAIT_TICKS);
    ESP_RETURN_ON_ERROR(err, TAG, "failed to load audio detail: %d(%s)", err, esp_err_to_name(err));

    i2s_chan_handle_t chan = writer->tx_chan;
    if (writer->chan_enable) {
        err = i2s_channel_disable(chan);
        if (ESP_OK != err) {
            ESP_LOGW(TAG, "failed to disable tx channel: %d(%s)", err, esp_err_to_name(err));
        }
    }

    if (detail.sample_rate > 0) {
        i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(detail.sample_rate);
        clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_768;
        err = i2s_channel_reconfig_std_clock(chan, &clk_cfg);
        if (ESP_OK != err) {
            return err;
        }
    }

    if (detail.bit_width >= 0) {
        i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(detail.bit_width, detail.channel);
        err = i2s_channel_reconfig_std_slot(chan, &slot_cfg);
        if (ESP_OK != err) {
            return err;
        }
    }

    writer->bit_width = detail.bit_width;
    if (writer->chan_enable) {
        i2s_channel_enable(writer->tx_chan);
    }
    return ESP_OK;
}

static bool amp_i2s_writer_process_notify(amp_i2s_writer_handle_t writer, struct amp_i2s_writer_task_state *state) {
    uint32_t notify = 0;
    EL_WAIT_NOTIFY(notify, state->event_wait_ticks) {
        EL_NOTIFY_ON_STOP(notify) {
            state->stopped = true;
            return true;
        }
        esp_err_t err;
        enum amp_state s = AMP_DASH_LOAD_STATE(writer->el_entry.dashboard);
        state->state = s == AMP_STATE_PLAYING ? IW_STATE_PLAYING : IW_STATE_WAIT_NOTIFY;

        EL_NOTIFY_ON_STATE(notify) {
            if (s == AMP_STATE_PAUSE && writer->chan_enable) {
                if ((err = i2s_channel_disable(writer->tx_chan)) != ESP_OK) {
                    ESP_LOGW(TAG, "failed to disable tx channel: %d(%s)", err, esp_err_to_name(err));
                }
                writer->chan_enable = false;
            } else if (s == AMP_STATE_PLAYING && !writer->chan_enable) {
                if ((err = i2s_channel_enable(writer->tx_chan)) != ESP_OK) {
                    ESP_LOGW(TAG, "failed to enable tx channel: %d(%s)", err, esp_err_to_name(err));
                }
                writer->chan_enable = true;
            }
        }
        EL_NOTIFY_ON_STREAM_NEW(notify) { state->new_stream = true; }
    }

    bool should_wait = state->state == IW_STATE_WAIT_NOTIFY;
    if (should_wait) {
        if (state->event_wait_ticks <= 0) {
            state->event_wait_ticks = AMP_I2S_WRITER_EVENT_WAIT_TICKS;
        }
    } else if (state->event_wait_ticks > 0) {
        state->event_wait_ticks = 0;
    }
    return should_wait;
}

/*
 * ############################################################
 * ############## element interface ###########################
 * ############################################################
 */

static void amp_i2s_writer_task(void *args) {
    amp_i2s_writer_handle_t writer = args;
    ringbuf_handle_t rb = writer->rb_in;
    assert(rb);

    size_t read_buf_size = 1020;
    uint8_t *read_buf = amp_malloc(sizeof(uint8_t) * read_buf_size);
    struct amp_i2s_writer_task_state task_state = {
        .state = AMP_DASH_LOAD_STATE(writer->el_entry.dashboard) == AMP_STATE_PLAYING ? IW_STATE_PLAYING
                                                                                      : IW_STATE_WAIT_NOTIFY,
        .event_wait_ticks = AMP_I2S_WRITER_EVENT_WAIT_TICKS,
        .new_stream = true,
        .stopped = false,
    };

    while (true) {
        if (task_state.stopped) {
            break;
        }
        if (amp_i2s_writer_process_notify(writer, &task_state)) {
            continue;
        }
        int data_size = rb_read(rb, (char *)read_buf, read_buf_size, AMP_I2S_WRITER_READ_WAIT_TICKS);
        if (RB_DONE == data_size) {
            ESP_LOGI(TAG, "input ringbuf done");
            task_state.new_stream = true;
            task_state.state = IW_STATE_WAIT_NOTIFY;
            amp_element_notify_event((amp_element_handle_t)writer, NOTIFY_VALUE_MASK_STREAM_END);
            continue;
        } else if (RB_ABORT == data_size) {
            ESP_LOGW(TAG, "input ringbuf aborted");
            task_state.new_stream = true;
            task_state.state = IW_STATE_WAIT_NOTIFY;
            amp_element_notify_event((amp_element_handle_t)writer, NOTIFY_VALUE_MASK_STREAM_ABORT);
            continue;
        } else if (RB_TIMEOUT == data_size) {
            ESP_LOGD(TAG, "read input ringbuf timeout");
            continue;
        } else if (RB_UNBLOCK == data_size || data_size < read_buf_size) {
            ESP_LOGI(TAG, "write ringbuf unblock, drop data, written: %d", data_size);
        } else if (data_size <= 0) {
            ESP_LOGE(TAG, "read input ringbuf failed: %d", data_size);
            continue;
        } else {
            ESP_LOGD(TAG, "read from ringbuf: %d bytes", data_size);
        }
        esp_err_t err = ESP_OK;
        if (task_state.new_stream) {
            err = amp_i2s_writer_config_output_slot(writer);
            if (ESP_OK == err) {
                task_state.new_stream = false;
            } else {
                ESP_LOGE(TAG, "set i2s output params fail: %d", err);
                continue;
            }
        }
        for (int retry = 0; retry < AMP_I2S_WRITER_WRITE_RETRY_COUNT; retry++) {
            err = amp_i2s_writer_write_pcm(writer, read_buf, data_size);
            if (ESP_OK == err) {
                break;
            } else if (ESP_ERR_INVALID_STATE == err) {
                ESP_LOGW(TAG, "I2S channel state is invalid, abort");
                break;
            }
            ESP_LOGW(TAG, "I2S write failed: %d(%s), retrying (%d/%d)", err, esp_err_to_name(err), retry + 1,
                     AMP_I2S_WRITER_WRITE_RETRY_COUNT);
        }
        if (ESP_OK == err) {
            ESP_LOGD(TAG, "wrote to I2S: %d bytes", data_size);
        }
    }

    amp_free(read_buf);
    amp_element_task_done((amp_element_handle_t)writer);
    vTaskDelete(NULL);
}

static void amp_i2s_writer_set_input(void *args, ringbuf_handle_t rb) {
    amp_i2s_writer_handle_t writer = args;
    writer->rb_in = rb;
}

static void amp_i2s_writer_el_deinit(void *args) { amp_i2s_writer_deinit((amp_i2s_writer_handle_t)args); }

static const amp_element_interface_t amp_i2s_writer_element_interface = {
    .deinit = amp_i2s_writer_el_deinit,
    .run_task = amp_i2s_writer_task,
    .set_input_rb = amp_i2s_writer_set_input,
    .set_output_rb = NULL,
};

/*
 * ############################################################
 * ########################## public #########################
 * ############################################################
 */

esp_err_t amp_i2s_writer_init(amp_i2s_writer_cfg_t *cfg, amp_i2s_writer_handle_t *writer) {
    amp_i2s_writer_handle_t w = amp_calloc(1, sizeof(struct i2s_writer));
    if (!w) {
        return ESP_ERR_NO_MEM;
    }
    w->i2s_port = cfg->i2s_port;
    amp_i2s_writer_setup_volume_table();
    amp_i2s_writer_set_volume(w, cfg->volume);
    esp_err_t err = amp_i2s_writer_driver_init(w, &cfg->gpio_cfg);
    if (ESP_OK != err) {
        amp_free(w);
        return err;
    }
    w->bit_width = I2S_DEFAULT_BIT_WIDTH;
    *writer = w;
    ESP_LOGD(TAG, "initialized i2s writer");
    return ESP_OK;
}

void amp_i2s_writer_deinit(amp_i2s_writer_handle_t writer) {
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

    amp_free(writer);
}

const amp_element_interface_t *amp_i2s_writer_get_element_interface(void) { return &amp_i2s_writer_element_interface; }

void amp_i2s_writer_set_volume(amp_i2s_writer_handle_t writer, uint8_t volume) {
    if (volume > 100) {
        ESP_LOGW(TAG, "volume value (%d) is invalid", volume);
        return;
    }
    int32_t gain;
    if (volume == 100) {
        gain = 1 << VOLUME_Q_FORMAT;
    } else if (volume == 0) {
        gain = 0;
    } else {
        gain = volume_table[volume - 1];
    }
    writer->volume_gain = gain;
}
