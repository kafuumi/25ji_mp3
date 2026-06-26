
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_log.h"

#include "amp/amp_mem.h"
#include "amp/audio_codec.h"
#include "amp/ringbuf.h"
#include "element_priv.h"

#define MAX_WAIT_TIME_TASK_NOTIFY pdMS_TO_TICKS(100)
#define MAX_WAIT_TIME_READ_RINGBUF pdMS_TO_TICKS(500)
#define MAX_WAIT_TIME_WRITE_RINGBUF pdMS_TO_TICKS(3000)

#define DO_WHAT_ON_FAIL(codec, counter)                                                                                \
    do {                                                                                                               \
        ++counter;                                                                                                     \
        if (counter) {                                                                                                 \
        }                                                                                                              \
    } while (0)

#define NOTIFY_VALUE_MASK_STATE 0x01 << 0
#define NOTIFY_VALUE_MASK_MEDIA 0x01 << 1

static const char *TAG = "audio_codec";

typedef enum audio_codec_state_t {
    CODEC_STATE_READY,
    CODEC_STATE_RUNNING,
} audio_codec_state_t;

struct audio_codec {
    AMP_ELEMENT_ENTRY() el_entry;   // must first
    ringbuf_handle_t rb_in, rb_out; /* input and output ringbuf. owner is others, read only*/

    esp_audio_simple_dec_handle_t decoder; /* simple decoder. owner is self*/
    bool decode_opened;                    /* decoder open flag */
    enum amp_audio_media_type media_type;
};

inline static esp_err_t audio_codec_create_decoder(audio_codec_handle_t codec,
                                                   const enum amp_audio_media_type media_type) {
    esp_audio_simple_dec_type_t dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    switch (media_type) {
    case AUDIO_MEDIA_TYPE_NONE:
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
    esp_audio_simple_dec_handle_t decoder;
    esp_err_t err = esp_audio_simple_dec_open(&dec_cfg, &decoder);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "open simple decoder fail: %s", esp_err_to_name(err));
        return err;
    }
    if (codec->decoder && codec->decode_opened) {
        ESP_LOGD(TAG, "close simple decoder");
        esp_audio_simple_dec_close(codec->decoder);
    }
    codec->decoder = decoder;
    codec->decode_opened = true;
    ESP_LOGI(TAG, "open simple decoder success, dec_type: %d", dec_type);
    return ESP_OK;
}

inline static bool setup_decoder(audio_codec_handle_t codec) {
    enum amp_audio_media_type media_type = codec->el_entry.dashboard->audio.media_type;
    if (media_type != codec->media_type) {
        codec->media_type = media_type;
    } else if (codec->decoder && codec->decode_opened) {
        return ESP_OK;
    }
    esp_err_t err = audio_codec_create_decoder(codec, media_type);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "create audio codec fail: %d(%s)", err, esp_err_to_name(err));
        return false;
    }
    return true;
}

struct audio_codec_task_state {
    bool is_first_dec;    // this stream is first to decode
    enum amp_state state; // cached state
    TickType_t wait_time; // wait task notify or event
    bool stop_task;
};

static bool audio_codec_receive_event(audio_codec_handle_t codec, struct audio_codec_task_state *task_state) {
    uint32_t notify = 0;
    bool no_media_type_notify = true;
    if (xTaskNotifyWait(0, ULONG_MAX, &notify, task_state->wait_time) == pdTRUE) {
    }
    bool notplay;
    if (task_state->is_first_dec && no_media_type_notify) {
        notplay = true;
    } else {
        notplay = task_state->state == AMP_STATE_PLAYING;
    }

    if (notplay) {
        task_state->wait_time = MAX_WAIT_TIME_TASK_NOTIFY;
    } else {
        task_state->wait_time = 0;
    }
    return notplay;
}

static void audio_codec_task_run(void *args) {
    audio_codec_handle_t codec = args;
    ringbuf_handle_t rb_in = codec->rb_in;
    ringbuf_handle_t rb_out = codec->rb_out;
    esp_audio_simple_dec_handle_t dec = NULL;
    assert(rb_in && rb_out);

    size_t rb_out_size = rb_get_size(rb_out);
    if (rb_out_size) {
        ESP_LOGI(TAG, "ringbuf out max size is %ld", rb_out_size);
    }

    size_t in_buf_size = 2048;
    uint8_t *in_buf = amp_malloc(sizeof(uint8_t) * in_buf_size);

    size_t out_buf_size = 2048;
    uint8_t *out_buf = amp_malloc(sizeof(uint8_t) * out_buf_size);

    esp_audio_simple_dec_raw_t raw_dec = {0};
    esp_audio_simple_dec_out_t out_dec = {0};
    out_dec.buffer = out_buf;
    out_dec.len = out_buf_size;

    esp_err_t err;
    int fail_counter = 0;
    struct audio_codec_task_state task_state = {
        .state = amp_dashboard_load_state(codec->el_entry.dashboard),
        .is_first_dec = true,
        .wait_time = MAX_WAIT_TIME_TASK_NOTIFY,
    };

_read_loop:
    while (true) {
        /* check task notify and handle event */
        if (audio_codec_receive_event(codec, &task_state)) {
            continue;
        }
        if (task_state.stop_task) {
            goto _task_end;
        }
        /* read data from input ringbuf */
        int in_size = rb_read(rb_in, (char *)in_buf, in_buf_size, MAX_WAIT_TIME_READ_RINGBUF);
        bool is_done = false;
        if (RB_DONE == in_size) {
            is_done = true;
            ESP_LOGW(TAG, "input ringbuf is done");
            if (raw_dec.eos) {
                // already handle is_done, continue
                continue;
            }
        } else if (RB_ABORT == in_size) {
            ESP_LOGW(TAG, "input ringbuf is abort");
            // abort data
            continue;
        } else if (RB_TIMEOUT == in_size) {
            ESP_LOGW(TAG, "read input ringbuf timeout");
            continue;
        } else if (RB_FAIL == in_size) {
            ESP_LOGW(TAG, "read input ringbuf fail, ignore");
            DO_WHAT_ON_FAIL(codec, fail_counter);
            continue;
        } else {
            ESP_LOGD(TAG, "read input ringbuf success, size: %d", in_size);
        }
        /* open esp audio codec */
        if (task_state.is_first_dec || !codec->decode_opened) {
            if (setup_decoder(codec)) {
                dec = codec->decoder;
                task_state.is_first_dec = false;
            } else {
                continue;
            }
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
                ESP_LOGW(TAG, "dec process fail, check dec: %p, raw: %p(%p), out: %p(%p)", dec, raw_dec, raw_dec.buffer,
                         out_dec, out_dec.buffer);
                DO_WHAT_ON_FAIL(codec, fail_counter);
                goto _read_loop;
            } else if (ESP_AUDIO_ERR_BUFF_NOT_ENOUGH == err) {
                ESP_LOGW(TAG, "output buffer not enough, try resize");
                size_t ns = out_dec.needed_size + out_buf_size;
                void *buf = amp_realloc(out_buf, ns);
                if (!buf) {
                    ESP_LOGW(TAG, "no enough memory to resize out buffer, need size: %d bytes", ns - out_buf_size);
                    DO_WHAT_ON_FAIL(codec, fail_counter);
                    continue;
                }
                ESP_LOGI(TAG, "realloc output buffer success, new size: %d bytes", ns);
                out_buf = buf;
                out_buf_size = ns;
                out_dec.buffer = out_buf;
                out_dec.len = out_buf_size;
                continue;
            } else if (ESP_AUDIO_ERR_NOT_SUPPORT == err) {
                ESP_LOGW(TAG, "dec input data is not supported, dec_type: %d", codec->media_type);
                DO_WHAT_ON_FAIL(codec, fail_counter);
                goto _read_loop;
            }
            ESP_LOGD(TAG, "decode success, consumed: %d, decodec_size: %d", raw_dec.consumed, out_dec.decoded_size);

            /* write pcm data to output ringbuf */
            if (out_dec.decoded_size > 0) {
                int write_size = 0;
                int try_count = 0;
            _try_write:
                write_size = rb_write(rb_out, (char *)out_buf, out_dec.decoded_size, MAX_WAIT_TIME_WRITE_RINGBUF);
                if (RB_DONE == write_size) {
                    ESP_LOGW(TAG, "output ringbuf is set to write done");
                } else if (RB_ABORT == write_size) {
                    ESP_LOGW(TAG, "output ringbuf is set to abort write");
                } else if (RB_TIMEOUT == write_size) {
                    ESP_LOGW(TAG, "write data to output ringbuf fail timeout");
                    /* retry */
                    try_count++;
                    if (try_count < 3) {
                        goto _try_write;
                    }
                } else if (write_size <= 0) {
                    ESP_LOGW(TAG, "write data to output ringbuf fail");
                    DO_WHAT_ON_FAIL(codec, fail_counter);
                } else {
                    ESP_LOGD(TAG, "write data to ringbuf success, size: %d", write_size);
                    // reset fail counter
                    fail_counter = 0;
                }
            }

            raw_dec.buffer += raw_dec.consumed;
            raw_dec.len -= raw_dec.consumed;
            if (raw_dec.eos) {
                /* end of stream, set done flag */
                rb_done_write(rb_out);
                task_state.is_first_dec = true;
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

static void audio_codec_set_input(void *args, ringbuf_handle_t rb_in) {
    audio_codec_handle_t decoder = args;
    decoder->rb_in = rb_in;
}

static void audio_codec_set_output(void *args, ringbuf_handle_t rb_out) {
    audio_codec_handle_t decoder = args;
    decoder->rb_out = rb_out;
}

static void audio_codec_el_deinit(void *args) { return audio_codec_deinit((audio_codec_handle_t)args); }

static const amp_element_interface_t audio_codec_element_interface = {
    .deinit = audio_codec_el_deinit,
    .set_input_rb = audio_codec_set_input,
    .set_output_rb = audio_codec_set_output,
    .task_run = audio_codec_task_run,
    .setup_event_handler = NULL,
};

const amp_element_interface_t *audio_codec_el_interface() { return &audio_codec_element_interface; }

esp_err_t audio_codec_init(audio_codec_handle_t *codec) {
    esp_audio_simple_dec_register_default();
    esp_audio_dec_register_default();
    audio_codec_handle_t c = amp_calloc(1, sizeof(struct audio_codec));
    if (!c)
        return ESP_ERR_NO_MEM;

    *codec = c;
    ESP_LOGD(TAG, "initialize audio codec success");
    return ESP_OK;
}

void audio_codec_deinit(audio_codec_handle_t codec) {
    if (!codec)
        return;

    if (codec->decoder && codec->decode_opened) {
        ESP_LOGD(TAG, "close simple decoder: %p", codec->decoder);
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
