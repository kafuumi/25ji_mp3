
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

static const char *TAG = "audio_codec";

struct audio_codec {
    AMP_ELEMENT_ENTRY() el_entry;   // must first
    ringbuf_handle_t rb_in, rb_out; /* input and output ringbuf. owner is others, read only*/

    esp_audio_simple_dec_handle_t decoder; /* simple decoder. owner is self*/
    bool decode_opened;                    /* decoder open flag */
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
    if (media_type == codec->media_type && codec->decoder && codec->decode_opened) {
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
    if (codec->decoder && codec->decode_opened) {
        ESP_LOGD(TAG, "closing previous decoder");
        esp_audio_simple_dec_close(codec->decoder);
    }
    codec->decoder = decoder;
    codec->decode_opened = true;
    ESP_LOGI(TAG, "opened decoder (type=%d)", dec_type);
    return true;
}

struct amp_audio_decoder_task_state {
    enum amp_state cached_state;
    TickType_t event_wait_ticks;
    bool stop_requested;
    bool new_stream;
    bool unknown_media;
};

static bool amp_audio_decoder_process_notify(amp_audio_decoder_handle_t codec, struct amp_audio_decoder_task_state *task_state) {
    uint32_t notify = 0;
    if (xTaskNotifyWait(0, ULONG_MAX, &notify, task_state->event_wait_ticks) == pdTRUE) {
        if (notify & NOTIFY_VALUE_MASK_STATE) {
            task_state->cached_state = AMP_DASH_LOAD_STATE(codec->el_entry.dashboard);
        }
        if (notify & NOTIFY_VALUE_MASK_MEDIA_TYPE) {
            task_state->new_stream = true;
            task_state->unknown_media = false;
        }
        if (notify & NOTIFY_VALUE_MASK_EOS_DONE) {
            task_state->new_stream = true;
        }
    }
    bool notplay;
    if (task_state->unknown_media) {
        notplay = true;
    } else {
        notplay = task_state->cached_state != AMP_STATE_PLAYING;
    }

    if (notplay) {
        if (task_state->event_wait_ticks <= 0) {
            task_state->event_wait_ticks = AMP_AUDIO_DECODER_EVENT_WAIT_TICKS;
        }
    } else if (task_state->event_wait_ticks > 0) {
        task_state->event_wait_ticks = 0;
    }
    return notplay;
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
        .cached_state = AMP_DASH_LOAD_STATE(codec->el_entry.dashboard),
        .unknown_media = true,
        .new_stream = false,
        .event_wait_ticks = AMP_AUDIO_DECODER_EVENT_WAIT_TICKS,
    };

_read_loop:
    while (true) {
        if (task_state.stop_requested) {
            goto _task_end;
        }
        /* check task notify and handle event */
        if (amp_audio_decoder_process_notify(codec, &task_state)) {
            continue;
        }

        /* open esp audio codec */
        if (task_state.new_stream || !codec->decode_opened) {
            if (amp_audio_decoder_setup(codec)) {
                dec = codec->decoder;
                task_state.new_stream = false;

            } else {
                continue;
            }
        }
        /* read data from input ringbuf */
        int in_size = rb_read(rb_in, (char *)in_buf, in_buf_size, AMP_AUDIO_DECODER_READ_WAIT_TICKS);
        bool is_done = false;
        if (RB_DONE == in_size) {
            is_done = true;
            task_state.unknown_media = true;
            ESP_LOGW(TAG, "input ringbuf done");
            if (raw_dec.eos) {
                // already handle is_done, continue
                continue;
            }
        } else if (RB_ABORT == in_size) {
            ESP_LOGW(TAG, "input ringbuf aborted");
            // abort data
            task_state.unknown_media = true;
            continue;
        } else if (RB_TIMEOUT == in_size) {
            ESP_LOGW(TAG, "read input ringbuf timeout");
            continue;
        } else if (RB_FAIL == in_size) {
            ESP_LOGW(TAG, "read input ringbuf failed");
                        fail_counter++;;
            continue;
        } else {
            ESP_LOGD(TAG, "read input ringbuf success, size: %d", in_size);
        }

        /* reset input and output */
        raw_dec.buffer = in_buf;
        raw_dec.len = is_done ? 0 : in_size;
        raw_dec.eos = is_done;
        raw_dec.frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE;

        while (raw_dec.len > 0 || raw_dec.eos) {
            // reset output and input
            raw_dec.consumed = 0;
            out_dec.needed_size = out_dec.decoded_size = 0;

            err = esp_audio_simple_dec_process(dec, &raw_dec, &out_dec);
            if (ESP_AUDIO_ERR_INVALID_PARAMETER == err) {
                ESP_LOGW(TAG, "decoder process failed");
                            fail_counter++;;
                goto _read_loop;
            } else if (ESP_AUDIO_ERR_BUFF_NOT_ENOUGH == err) {
                ESP_LOGW(TAG, "output buffer too small, resizing");
                size_t ns = out_dec.needed_size + out_buf_size;
                void *buf = amp_realloc(out_buf, ns);
                if (!buf) {
                    ESP_LOGW(TAG, "not enough memory to resize output buffer (need %d bytes)", ns - out_buf_size);
                                fail_counter++;;
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
                            fail_counter++;;
                goto _read_loop;
            }
            ESP_LOGD(TAG, "decoded %d bytes (consumed %d)", out_dec.decoded_size, raw_dec.consumed);

            /* write pcm data to output ringbuf */
            if (out_dec.decoded_size > 0) {
                int write_size = 0;
                int try_count = 0;
            _try_write:
                write_size = rb_write(rb_out, (char *)out_buf, out_dec.decoded_size, AMP_AUDIO_DECODER_WRITE_WAIT_TICKS);
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
                } else if (write_size <= 0) {
                    ESP_LOGW(TAG, "write to output ringbuf failed");
                                fail_counter++;;
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
                AMP_EL_SEND_DONE(TAG, codec, el_entry);
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

static void amp_audio_decoder_el_deinit(void *args) { return amp_audio_decoder_deinit((amp_audio_decoder_handle_t)args); }

static void amp_audio_decoder_report_event_handler(void *args, esp_event_base_t base_id, int32_t evt_id, void *evt_data) {
    amp_audio_decoder_handle_t codec = args;
    TaskHandle_t task = codec->el_entry.task;
    ESP_LOGD(TAG, "received event: %d", evt_id);
    switch (evt_id) {
    case AMP_EVENT_REPORT_AUDIO_FORMAT:
        xTaskNotify(task, NOTIFY_VALUE_MASK_MEDIA_TYPE, eSetBits);
        break;
    }
}

static esp_err_t amp_audio_decoder_register_events(void *args, esp_event_loop_handle_t event_bus) {
    esp_err_t err = esp_event_handler_instance_register_with(event_bus, AMP_EVENT_REPORT, AMP_EVENT_REPORT_AUDIO_FORMAT,
                                                             amp_audio_decoder_report_event_handler, args, NULL);
    return err;
}

static const amp_element_interface_t amp_audio_decoder_element_interface = {
    .deinit = amp_audio_decoder_el_deinit,
    .set_input_rb = amp_audio_decoder_set_input,
    .set_output_rb = amp_audio_decoder_set_output,
    .run_task = amp_audio_decoder_task_run,
    .register_events = amp_audio_decoder_register_events,
};

const amp_element_interface_t *amp_audio_decoder_get_element_interface() { return &amp_audio_decoder_element_interface; }

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

    if (codec->decoder && codec->decode_opened) {
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
