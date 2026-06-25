
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_log.h"

#include "amp/amp_mem.h"
#include "amp/audio_codec.h"
#include "amp/ringbuf.h"
#include "element_priv.h"

static const char *TAG = "audio_codec";

struct audio_codec {
    AMP_ELEMENT_ENTRY() el_entry;   // must first
    ringbuf_handle_t rb_in, rb_out; /* input and output ringbuf. owner is others, read only*/

    esp_audio_simple_dec_handle_t decoder; /* simple decoder. owner is self*/
    bool decode_opened;                    /* decoder open flag */
    esp_audio_simple_dec_type_t dec_type;
};

static inline esp_err_t audio_codec_create_decoder(audio_codec_handle_t codec,
                                                   const esp_audio_simple_dec_type_t dec_type) {
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

static void audio_codec_task_run(void *args) {
    audio_codec_handle_t decoder = args;
    ringbuf_handle_t rb_in = decoder->rb_in;
    ringbuf_handle_t rb_out = decoder->rb_out;
    esp_audio_simple_dec_handle_t dec = decoder->decoder;
    assert(rb_in && rb_out);
    assert(dec);

    size_t rb_out_size = rb_get_size(rb_out);
    if (rb_out_size) {
        ESP_LOGI(TAG, "ringbuf out max size is %ld", rb_out_size);
    }

    size_t in_buf_size = 2048;
    uint8_t *in_buf = amp_malloc(sizeof(uint8_t) * in_buf_size);

    size_t out_buf_size = 2048;
    uint8_t *out_buf = amp_malloc(sizeof(uint8_t) * out_buf_size);

    TickType_t read_wait = pdMS_TO_TICKS(100);
    TickType_t write_wait = portMAX_DELAY;

    esp_audio_simple_dec_raw_t raw_dec = {0};
    esp_audio_simple_dec_out_t out_dec = {0};
    esp_err_t err;

_read_loop:
    while (true) {
        int in_size = rb_read(rb_in, (char *)in_buf, in_buf_size, read_wait);
        bool is_done = false;
        if (RB_DONE == in_size) {
            is_done = true;
            ESP_LOGW(TAG, "read data is done");
        } else if (in_size < 0) {
            ESP_LOGE(TAG, "read data from ringbuf fail: %d", in_size);
            continue;
        } else {
            ESP_LOGD(TAG, "read data from ringbuf size: %d", in_size);
        }
        raw_dec.buffer = in_buf;
        raw_dec.len = is_done ? 0 : in_size;
        raw_dec.eos = is_done;
        raw_dec.frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE;

        while (raw_dec.len > 0 || is_done) {
            // reset output and input
            raw_dec.consumed = 0;
            out_dec.needed_size = out_dec.decoded_size = 0;

            err = esp_audio_simple_dec_process(dec, &raw_dec, &out_dec);
            if (ESP_AUDIO_ERR_BUFF_NOT_ENOUGH == err) {
                ESP_LOGW(TAG, "output buffer not enough, try resize");
                size_t ns = out_dec.needed_size + out_buf_size;
                void *buf = amp_realloc(out_buf, ns);
                if (!buf) {
                    ESP_LOGW(TAG, "no enough memory, need size: %d bytes", ns);
                    // todo: error handle
                    continue;
                }
                ESP_LOGI(TAG, "realloc output buffer success, new size: %d bytes", ns);
                out_buf = buf;
                out_buf_size = ns;
                out_dec.buffer = out_buf;
                out_dec.len = out_buf_size;
                continue;
            } else if (err < 0) {
                ESP_LOGW(TAG, "decode fail: %d", err);
                continue;
            }
            ESP_LOGI(TAG, "decode success, consumed: %d, decodec_size: %d", raw_dec.consumed, out_dec.decoded_size);
            if (out_dec.decoded_size > 0) {
                // write data
                err = rb_write(rb_out, (char *)out_buf, out_dec.decoded_size, write_wait);
                if (err < 0) {
                    ESP_LOGW(TAG, "write pcm data to ringbuf fail: %d", err);
                    continue;
                }
            }

            raw_dec.buffer += raw_dec.consumed;
            raw_dec.len -= raw_dec.consumed;
            if (is_done) {
                rb_done_write(rb_out);
                amp_dashboard_send_done(decoder->el_entry.dashboard);
                goto _read_loop;
            }
        }
    }

    if (in_buf)
        amp_free(in_buf);
    if (out_buf)
        amp_free(out_buf);
}

static void audio_codec_set_input(void *args, ringbuf_handle_t rb_in) {
    audio_codec_handle_t decoder = args;
    decoder->rb_in = rb_in;
}

static void audio_codec_set_output(void *args, ringbuf_handle_t rb_out) {
    audio_codec_handle_t decoder = args;
    decoder->rb_out = rb_out;
}

static const amp_element_interface_t audio_codec_element_interface = {
    .deinit = NULL,
    .set_input_rb = audio_codec_set_input,
    .set_output_rb = audio_codec_set_output,
    .task_run = audio_codec_task_run,
};

const amp_element_interface_t *audio_codec_el_interface() { return &audio_codec_element_interface; }

esp_err_t audio_codec_init(audio_codec_handle_t *codec) {
    esp_audio_simple_dec_register_default();
    esp_audio_dec_register_default();
    audio_codec_handle_t c = amp_calloc(1, sizeof(struct audio_codec));
    if (!c)
        return ESP_ERR_NO_MEM;

    audio_codec_create_decoder(c, ESP_AUDIO_SIMPLE_DEC_TYPE_MP3);
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

#if defined(APP_RUN_TEST_MODE)

#endif // APP_RUN_TEST_MODE
