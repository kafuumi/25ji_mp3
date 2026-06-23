
#include "esp_audio_simple_dec.h"
#include "esp_log.h"

#include "amp/audio_codec.h"
#include "element_priv.h"

static const char *TAG = "audio_codec";

struct audio_codec {
    AMP_ELEMENT_ENTRY() el_entry;
    RingbufHandle_t rb_in, rb_out;
    bool decode_opened;
    esp_audio_simple_dec_handle_t decoder;
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
    ESP_LOGE(TAG, "open simple decoder success, dec_type: %d", dec_type);
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
        ESP_LOGI(TAG, "ringbuf out max size is %zu", rb_out_size);
    }

    esp_audio_simple_dec_raw_t raw_info = {0};
    esp_audio_simple_dec_out_t out_info = {0};
    size_t receive_size = 0;
    uint8_t *out_buf = malloc(sizeof(uint8_t) * 1024);
    while (true) {
        void *item = xRingbufferReceive(rb_in, &receive_size, portMAX_DELAY);
        raw_info.buffer = item;
        raw_info.len = receive_size;
        while (raw_info.len > 0) {
            out_info.buffer = out_buf;
            out_info.len = 1024;
            esp_audio_simple_dec_process(dec, &raw_info, &out_info);
            xRingbufferSend(rb_out, out_buf, out_info.decoded_size, portMAX_DELAY);
            raw_info.buffer += raw_info.consumed;
            raw_info.len -= raw_info.consumed;
        }
        vRingbufferReturnItem(rb_in, item);
    }
}

static void audio_decodc_set_input(void *args, RingbufHandle_t rb_in) {
    audio_codec_handle_t *decoder = args;
    decoder->rb_in = rb_in;
}

static void audio_decodec_set_output(void *args, RingbufHandle_t rb_out) {
    audio_codec_handle_t *decoder = args;
    decoder->rb_out = rb_out;
}

esp_err_t audio_codec_init(audio_codec_handle_t **codec) { return ESP_OK; }
