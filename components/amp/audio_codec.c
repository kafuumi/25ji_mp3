
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_log.h"

#include "amp/audio_codec.h"
#include "element_priv.h"

static const char *TAG = "audio_codec";

struct audio_codec {
    AMP_ELEMENT_ENTRY() el_entry;  // must first
    RingbufHandle_t rb_in, rb_out; /* input and output ringbuf. owner is others, read only*/

    esp_audio_simple_dec_handle_t decoder; /* simple decoder. owner is self*/
    bool decode_opened;                    /* decoder open flag */
    esp_audio_simple_dec_type_t dec_type;
};

static inline esp_err_t audio_codec_create_decoder(audio_codec_handle_t *codec,
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
    audio_codec_handle_t *decoder = args;
    RingbufHandle_t rb_in = decoder->rb_in;
    RingbufHandle_t rb_out = decoder->rb_out;
    esp_audio_simple_dec_handle_t dec = decoder->decoder;
    assert(rb_in && rb_out);
    size_t rb_out_size = xRingbufferGetMaxItemSize(rb_out);
    if (rb_out_size) {
        ESP_LOGI(TAG, "ringbuf out max size is %ld", rb_out_size);
    }

    size_t out_buf_size = 2048;
    uint8_t *out_buf = malloc(sizeof(uint8_t) * out_buf_size);

    esp_err_t err;
    while (true) {
        size_t in_size = 0;
        void *in_data = xRingbufferReceive(rb_in, &in_size, pdMS_TO_TICKS(100));
        if (in_size == 0) {
            continue;
        }
        esp_audio_simple_dec_raw_t raw = {
            .buffer = in_data,
            .len = in_size,
            .eos = false,
        };
        esp_audio_simple_dec_out_t out = {
            .buffer = out_buf,
            .len = out_buf_size,
        };
        while (raw.len > 0) {
            err = esp_audio_simple_dec_process(dec, &raw, &out);
            if (ESP_AUDIO_ERR_OK == err) {
                ESP_LOGI(TAG, "decode success, size: %d, consumed: %d", out.decoded_size, raw.consumed);
                if (out.decoded_size > 0)
                    xRingbufferSend(rb_out, out.buffer, out.decoded_size, portMAX_DELAY);
                raw.buffer += raw.consumed;
                raw.len -= raw.consumed;
                continue;
            } else if (ESP_AUDIO_ERR_BUFF_NOT_ENOUGH == err) {
                ESP_LOGW(TAG, "buffer not enough");
                size_t ns = out.needed_size + out_buf_size;
                out_buf = realloc(out_buf, ns);
                out.buffer = out_buf;
                out.len = ns;
                out_buf_size = ns;
                continue;
            }
        }
        vRingbufferReturnItem(rb_in, in_data);
    }
}

static void audio_codec_set_input(void *args, RingbufHandle_t rb_in) {
    audio_codec_handle_t *decoder = args;
    decoder->rb_in = rb_in;
}

static void audio_codec_set_output(void *args, RingbufHandle_t rb_out) {
    audio_codec_handle_t *decoder = args;
    decoder->rb_out = rb_out;
}

static const amp_element_interface_t audio_codec_element_interface = {
    .deinit = NULL,
    .set_input_rb = audio_codec_set_input,
    .set_output_rb = audio_codec_set_output,
    .task_run = audio_codec_task_run,
};

const amp_element_interface_t *audio_codec_el_interface() { return &audio_codec_element_interface; }

esp_err_t audio_codec_init(audio_codec_handle_t **codec) {
    esp_audio_simple_dec_register_default();
    esp_audio_dec_register_default();
    audio_codec_handle_t *c = malloc(sizeof(audio_codec_handle_t));
    if (!c)
        return ESP_ERR_NO_MEM;
    memset(c, 0, sizeof(audio_codec_handle_t));

    audio_codec_create_decoder(c, ESP_AUDIO_SIMPLE_DEC_TYPE_MP3);
    *codec = c;
    ESP_LOGD(TAG, "initialize audio codec success");
    return ESP_OK;
}

void audio_codec_deinit(audio_codec_handle_t *codec) {
    if (!codec)
        return;

    if (codec->decoder && codec->decode_opened) {
        ESP_LOGD(TAG, "close simple decoder: %p", codec->decoder);
        esp_audio_simple_dec_close(codec->decoder);
    }
    free(codec);
}

#if defined(APP_RUN_TEST_MODE)

#endif // APP_RUN_TEST_MODE
