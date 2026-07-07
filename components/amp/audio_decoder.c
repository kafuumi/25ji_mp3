
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_log.h"

#include "amp/amp_event.h"
#include "amp/amp_mem.h"
#include "amp/audio_decoder.h"
#include "amp/ringbuf.h"
#include "element_priv.h"

#define AMP_AUDIO_DECODER_EVENT_WAIT_TICKS pdMS_TO_TICKS(100)
#define AMP_AUDIO_DECODER_READ_WAIT_TICKS pdMS_TO_TICKS(500)
#define AMP_AUDIO_DECODER_WRITE_WAIT_TICKS pdMS_TO_TICKS(3000)
#define AMP_AUDIO_DECODER_POST_EVENT_WAIT_TICKS pdMS_TO_TICKS(1000)

static const char *TAG = "audio_codec";

struct audio_codec {
    AMP_ELEMENT_ENTRY() el_entry;   // must first
    ringbuf_handle_t rb_in, rb_out; /* input and output ringbuf. owner is others, read only*/

    esp_audio_simple_dec_handle_t decoder; /* simple decoder. owner is self*/
    enum amp_audio_media_type media_type;
};

static bool amp_audio_decoder_setup(amp_audio_decoder_handle_t codec) {
    esp_audio_simple_dec_type_t dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
    enum amp_audio_media_type media_type = AMP_DASH_LOAD_MEDIA_TYPE(codec->el_entry.dashboard);
    switch (media_type) {
    case AUDIO_MEDIA_TYPE_NONE:
        ESP_LOGW(TAG, "unknown audio media type, waiting for format event");
        return false;
    case AUDIO_MEDIA_TYPE_MP3:
        dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
        break;
    case AUDIO_MEDIA_TYPE_AAC:
        dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
        break;
    case AUDIO_MEDIA_TYPE_FLAC:
        dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
        break;
    }

    esp_audio_simple_dec_cfg_t dec_cfg = {
        .dec_type = dec_type,
        .use_frame_dec = false,
    };
    esp_err_t err;
    if (media_type == codec->media_type && codec->decoder) {
        // reset
        err = esp_audio_simple_dec_reset(codec->decoder);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "failed to reset decoder: %d(%s)", err, esp_err_to_name(err));
            return false;
        }
        return true;
    }
    // open new decoder
    esp_audio_simple_dec_handle_t decoder;
    err = esp_audio_simple_dec_open(&dec_cfg, &decoder);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "failed to open decoder: %s", esp_err_to_name(err));
        return false;
    }
    codec->media_type = media_type;
    if (codec->decoder) {
        ESP_LOGD(TAG, "closing previous decoder");
        esp_audio_simple_dec_close(codec->decoder);
    }
    codec->decoder = decoder;
    ESP_LOGI(TAG, "opened decoder to decode %d", media_type);
    return true;
}

static bool amp_audio_decoder_get_media_info(amp_audio_decoder_handle_t decoder) {
    esp_audio_simple_dec_info_t media_info;
    esp_err_t err = esp_audio_simple_dec_get_info(decoder->decoder, &media_info);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "get media info fail: %d(%s)", err, esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "media sample rate: %d hz channel: %d, %d bit %d kbps", media_info.sample_rate, media_info.channel,
             media_info.bits_per_sample, media_info.bitrate);
    struct amp_audio_detail detail = {
        .bitrate = media_info.bitrate,
        .bit_width = media_info.bits_per_sample,
        .sample_rate = media_info.sample_rate,
        .channel = media_info.channel,
    };
    err =
        amp_dashboard_swap_audio_detail(decoder->el_entry.dashboard, &detail, AMP_AUDIO_DECODER_POST_EVENT_WAIT_TICKS);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "swap audio detail timeout");
        return false;
    }
    if (detail.bit_width != media_info.bits_per_sample || detail.sample_rate != media_info.sample_rate ||
        detail.channel != media_info.channel) {
        /* post event */
        err = esp_event_post_to(decoder->el_entry.event_bus, AMP_EVENT_REPORT, AMP_EVENT_REPORT_AUDIO_DETAIL, 0, 0,
                                AMP_AUDIO_DECODER_POST_EVENT_WAIT_TICKS);
        if (ESP_OK != err) {
            ESP_LOGW(TAG, "post AUDIO DETAIL event fail: %d(%s)", err, esp_err_to_name(err));
        }
    }

    return true;
}

typedef enum {
    AD_STATE_PLAYING,
    AD_STATE_WAIT_NOTIFY,
} amp_audio_decoder_state_t;

struct amp_audio_decoder_task_state {
    TickType_t event_wait_ticks;
    amp_audio_decoder_state_t state;
    bool stopped;
    bool first_dec;
    bool new_stream;
};

static bool amp_audio_decoder_process_notify(amp_audio_decoder_handle_t codec,
                                             struct amp_audio_decoder_task_state *task_state) {
    uint32_t notify = 0;
    EL_WAIT_NOTIFY(notify, task_state->event_wait_ticks) {
        task_state->state = AMP_DASH_IS_PLAYING(codec->el_entry.dashboard) ? AD_STATE_PLAYING : AD_STATE_WAIT_NOTIFY;
        EL_NOTIFY_ON_STREAM_NEW(notify) {
            ESP_LOGI(TAG, "receive STREAM NEW notify");
            task_state->new_stream = true;
            task_state->first_dec = true;
        }
    }
    bool should_wait = task_state->state == AD_STATE_WAIT_NOTIFY;
    if (should_wait) {
        if (task_state->event_wait_ticks <= 0) {
            task_state->event_wait_ticks = AMP_AUDIO_DECODER_EVENT_WAIT_TICKS;
        }
    } else if (task_state->event_wait_ticks > 0) {
        task_state->event_wait_ticks = 0;
    }
    return should_wait;
}

static void amp_audio_decoder_task_run(void *args) {
    amp_audio_decoder_handle_t codec = args;
    ringbuf_handle_t rb_in = codec->rb_in;
    ringbuf_handle_t rb_out = codec->rb_out;
    esp_audio_simple_dec_handle_t dec = NULL;
    assert(rb_in && rb_out);

    size_t rb_out_size = rb_get_size(rb_out);
    if (rb_out_size) {
        ESP_LOGD(TAG, "output ringbuf size: %ld bytes", rb_out_size);
    }

    size_t in_buf_size = 2048;
    uint8_t *in_buf = amp_malloc(sizeof(uint8_t) * in_buf_size);

    size_t out_buf_size = 2048;
    uint8_t *out_buf = amp_malloc(sizeof(uint8_t) * out_buf_size);

    esp_audio_simple_dec_raw_t raw_dec = {0};
    esp_audio_simple_dec_out_t out_dec = {0};
    out_dec.buffer = out_buf;
    out_dec.len = out_buf_size;
    codec->media_type = AUDIO_MEDIA_TYPE_NONE;

    esp_err_t err;
    int fail_counter = 0;
    struct amp_audio_decoder_task_state task_state = {
        .state = AMP_DASH_IS_PLAYING(codec->el_entry.dashboard) ? AD_STATE_PLAYING : AD_STATE_WAIT_NOTIFY,
        .event_wait_ticks = AMP_AUDIO_DECODER_EVENT_WAIT_TICKS,
        .stopped = false,
        .first_dec = true,
        .new_stream = true,
    };

_read_loop:
    while (true) {
        if (task_state.stopped) {
            goto _task_end;
        }
        /* check task notify and handle event */
        if (amp_audio_decoder_process_notify(codec, &task_state)) {
            continue;
        }

        /* read data from input ringbuf */
        int in_size = rb_read(rb_in, (char *)in_buf, in_buf_size, AMP_AUDIO_DECODER_READ_WAIT_TICKS);
        if (RB_DONE == in_size) {
            ESP_LOGW(TAG, "input ringbuf done");
            if (raw_dec.eos) {
                // already handle is_done, continue
                continue;
            }
            in_size = 0;
            task_state.new_stream = true;
            task_state.first_dec = true;
            task_state.state = AD_STATE_WAIT_NOTIFY;
        } else if (RB_ABORT == in_size) {
            ESP_LOGW(TAG, "input ringbuf aborted");
            // abort data
            task_state.new_stream = true;
            task_state.first_dec = true;
            task_state.state = AD_STATE_WAIT_NOTIFY;
            continue;
        } else if (RB_TIMEOUT == in_size) {
            ESP_LOGW(TAG, "read input ringbuf timeout");
            continue;
        } else if (RB_FAIL == in_size) {
            ESP_LOGW(TAG, "read input ringbuf failed");
            fail_counter++;
            ;
            continue;
        } else {
            ESP_LOGD(TAG, "read input ringbuf success, size: %d", in_size);
        }

        /* open esp audio codec */
        if (task_state.new_stream || !codec->decoder) {
            if (amp_audio_decoder_setup(codec)) {
                dec = codec->decoder;
                task_state.new_stream = false;
            } else {
                continue;
            }
        }

        /* reset input and output */
        raw_dec.buffer = in_buf;
        raw_dec.len = in_size;
        raw_dec.eos = in_size == 0;
        raw_dec.frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE;

        while (raw_dec.len > 0 || raw_dec.eos) {
            // reset output and input
            raw_dec.consumed = 0;
            out_dec.needed_size = out_dec.decoded_size = 0;

            err = esp_audio_simple_dec_process(dec, &raw_dec, &out_dec);
            if (ESP_AUDIO_ERR_INVALID_PARAMETER == err) {
                ESP_LOGW(TAG, "decoder process failed");
                fail_counter++;
                goto _read_loop;
            } else if (ESP_AUDIO_ERR_BUFF_NOT_ENOUGH == err) {
                ESP_LOGW(TAG, "output buffer too small, resizing");
                size_t ns = out_dec.needed_size + out_buf_size;
                void *buf = amp_realloc(out_buf, ns);
                if (!buf) {
                    ESP_LOGW(TAG, "not enough memory to resize output buffer (need %d bytes)", ns - out_buf_size);
                    fail_counter++;
                    continue;
                }
                ESP_LOGI(TAG, "resized output buffer to %d bytes", ns);
                out_buf = buf;
                out_buf_size = ns;
                out_dec.buffer = out_buf;
                out_dec.len = out_buf_size;
                continue;
            } else if (ESP_AUDIO_ERR_NOT_SUPPORT == err) {
                ESP_LOGW(TAG, "unsupported decoder input (type=%d)", codec->media_type);
                fail_counter++;
                goto _read_loop;
            }
            ESP_LOGD(TAG, "decoded %d bytes (consumed %d)", out_dec.decoded_size, raw_dec.consumed);

            /* read media info */
            if (out_dec.decoded_size > 0 && task_state.first_dec && amp_audio_decoder_get_media_info(codec)) {
                task_state.first_dec = false;
            }

            /* write pcm data to output ringbuf */
            if (out_dec.decoded_size > 0) {
                int write_size = 0;
                int try_count = 0;
            _try_write:
                write_size =
                    rb_write(rb_out, (char *)out_buf, out_dec.decoded_size, AMP_AUDIO_DECODER_WRITE_WAIT_TICKS);
                if (RB_DONE == write_size) {
                    ESP_LOGW(TAG, "output ringbuf is set to write done");
                } else if (RB_ABORT == write_size) {
                    ESP_LOGW(TAG, "output ringbuf is set to abort write");
                } else if (RB_TIMEOUT == write_size) {
                    ESP_LOGW(TAG, "write to output ringbuf timed out");
                    /* retry */
                    try_count++;
                    if (try_count < 3) {
                        goto _try_write;
                    }
                    goto _read_loop;
                } else if (write_size <= 0) {
                    ESP_LOGW(TAG, "write to output ringbuf failed");
                    fail_counter++;
                } else {
                    ESP_LOGD(TAG, "wrote to output ringbuf: %d bytes", write_size);
                    // reset fail counter
                    fail_counter = 0;
                }
            }

            raw_dec.buffer += raw_dec.consumed;
            raw_dec.len -= raw_dec.consumed;
            if (raw_dec.eos) {
                /* end of stream, set done flag */
                rb_done_write(rb_out);
                amp_element_notify_event((amp_element_handle_t)codec, NOTIFY_VALUE_MASK_STREAM_END);
                goto _read_loop;
            }
        }
    }

_task_end:
    if (in_buf)
        amp_free(in_buf);
    if (out_buf)
        amp_free(out_buf);
    vTaskDelete(NULL);
}

static void amp_audio_decoder_set_input(void *args, ringbuf_handle_t rb_in) {
    amp_audio_decoder_handle_t decoder = args;
    decoder->rb_in = rb_in;
}

static void amp_audio_decoder_set_output(void *args, ringbuf_handle_t rb_out) {
    amp_audio_decoder_handle_t decoder = args;
    decoder->rb_out = rb_out;
}

static void amp_audio_decoder_el_deinit(void *args) {
    return amp_audio_decoder_deinit((amp_audio_decoder_handle_t)args);
}

static const amp_element_interface_t amp_audio_decoder_element_interface = {
    .deinit = amp_audio_decoder_el_deinit,
    .set_input_rb = amp_audio_decoder_set_input,
    .set_output_rb = amp_audio_decoder_set_output,
    .run_task = amp_audio_decoder_task_run,
};

const amp_element_interface_t *amp_audio_decoder_get_element_interface() {
    return &amp_audio_decoder_element_interface;
}

esp_err_t amp_audio_decoder_init(amp_audio_decoder_handle_t *codec) {
    esp_audio_simple_dec_register_default();
    esp_audio_dec_register_default();
    amp_audio_decoder_handle_t c = amp_calloc(1, sizeof(struct audio_codec));
    if (!c)
        return ESP_ERR_NO_MEM;

    *codec = c;
    ESP_LOGD(TAG, "initialized audio decoder");
    return ESP_OK;
}

void amp_audio_decoder_deinit(amp_audio_decoder_handle_t codec) {
    if (!codec)
        return;

    if (codec->decoder) {
        ESP_LOGD(TAG, "closed decoder");
        esp_audio_simple_dec_close(codec->decoder);
    }
    amp_free(codec);
}

volatile static bool is_registeied = false;

esp_err_t audio_codec_register() {
    if (!is_registeied) {
        esp_audio_err_t err;
        bool ok = (((err = esp_audio_dec_register_default()) == ESP_AUDIO_ERR_OK) &&
                   (err = esp_audio_simple_dec_register_default() == ESP_AUDIO_ERR_OK));
        if (ok) {
            is_registeied = true;
            return ESP_OK;
        }
        return err;
    }
    return ESP_OK;
}
