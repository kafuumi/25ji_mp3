
#include <math.h>

#include "esp_err.h"
#include "esp_log.h"

#include "amp/amp_mem.h"
#include "amp/controller.h"
#include "amp/ringbuf.h"
#include "amp/sin_pcm_reader.h"
#include "element_priv.h"

static const char *TAG = "sin_pcm";

struct sin_pcm_reader {
    AMP_ELEMENT_ENTRY() el_entry;
    int max_amplitude;
    size_t frames_size;
    ringbuf_handle_t rb_out;
    amp_sine_pcm_audio_config_t args;
};

static void generate_sin_pcm_16bit(amp_sine_pcm_reader_handle_t reader, int16_t *buf, float *phase) {
    const amp_sine_pcm_audio_config_t *args = &(reader->args);
    const float amplitude = reader->max_amplitude;
    float tmp = 0;
    if (phase) {
        tmp = *phase;
    }
    const float pi2 = (float)2.0f * M_PI;
    for (size_t i = 0; i < reader->frames_size; ++i) {
        float delta = pi2 * args->freq / args->sample_rate;
        tmp += delta;
        if (tmp >= pi2) {
            tmp -= pi2;
        }
        int16_t value = (int16_t)lrintf(sinf(tmp) * amplitude * (args->volume / 100.));
        *buf = value;
        buf++;
        if (args->channel == AUDIO_CHANNEL_STEREO) {
            // write right channel
            *buf = value;
            buf++;
        }
    }
    *phase = tmp;
}

static bool amp_sine_pcm_reader_process_notify(amp_sine_pcm_reader_handle_t reader, TickType_t wait_time) {
    // xxxx-xxxx xxxx-xxxx xxxx-xxxx xxxx-xxxx
    // undefined    self     REPORT   ACTION
    uint32_t notify_count = 0;
    if (xTaskNotifyWait(0, ULONG_MAX, &notify_count, wait_time)) {
        ESP_LOGI(TAG, "receive event notify: %d", notify_count);
    }
    // enum amp_state state = AMP_EL_SEND_DONE(TAG, reader, el_entry);
    // return state == AMP_STATE_PLAYING;
    return true;
}

static void amp_sine_pcm_reader_task(void *args) {
    amp_sine_pcm_reader_handle_t reader = (amp_sine_pcm_reader_handle_t)args;
    size_t buf_size = reader->frames_size * reader->args.channel * sizeof(int16_t);
    ringbuf_handle_t rb = reader->rb_out;
    assert(rb);

    size_t rb_max_size = rb_get_size(rb);
    if (rb_max_size < buf_size) {
        ESP_LOGE(TAG, "ringbuf max size(%lu) is less than frames_size * channel(%lu)", rb_max_size, buf_size);
        vTaskDelete(NULL);
        return;
    }
    const TickType_t max_wait = pdMS_TO_TICKS(1000);

    float phase = 0;
    void *buf = amp_malloc(buf_size);
    TickType_t wait_time = 0;
    while (true) {
        bool should_gen = amp_sine_pcm_reader_process_notify(reader, wait_time);
        if (!should_gen) {
            wait_time = pdMS_TO_TICKS(100);
            continue;
        }
        wait_time = 0;

        // generate pcm data
        generate_sin_pcm_16bit(reader, (int16_t *)buf, &phase);
        int write_size = rb_write(rb, buf, buf_size, max_wait);
        if (write_size <= 0) {
            ESP_LOGW(TAG, "send to ringbuf fail: %d", write_size);
        } else if (write_size < buf_size) {
            ESP_LOGW(TAG, "send ringbuf size %d less than buf size %d", write_size, buf_size);
        }
    }
}

static esp_err_t amp_sine_pcm_reader_register_events(void *args, esp_event_loop_handle_t event_bus) { return ESP_OK; }

static void amp_sine_pcm_reader_set_output(void *args, ringbuf_handle_t rb) {
    amp_sine_pcm_reader_handle_t reader = args;
    reader->rb_out = rb;
}

static const amp_element_interface_t amp_sine_pcm_element_interface = {
    .run_task = amp_sine_pcm_reader_task,
    .set_input_rb = NULL,
    .set_output_rb = amp_sine_pcm_reader_set_output,
    .register_events = amp_sine_pcm_reader_register_events,
};

// #####################################################################
// ####################### sin_pcm_reader public #######################
// #####################################################################

esp_err_t amp_sine_pcm_reader_init(amp_sine_pcm_reader_cfg_t *cfg, amp_sine_pcm_reader_handle_t *reader) {
    amp_sine_pcm_reader_handle_t r = amp_calloc(1, sizeof(struct sin_pcm_reader));
    if (!r) {
        return ESP_ERR_NO_MEM;
    }
    r->frames_size = cfg->frames_size;
    r->max_amplitude = cfg->max_amplitude;
    *reader = r;
    return ESP_OK;
}

void amp_sine_pcm_reader_deinit(amp_sine_pcm_reader_handle_t reader) {
    if (!reader) {
        return;
    }
    amp_free(reader);
}

void amp_sine_pcm_reader_set_audio_config(amp_sine_pcm_reader_handle_t reader,
                                          const amp_sine_pcm_audio_config_t *args) {
    memcpy(&(reader->args), args, sizeof(amp_sine_pcm_audio_config_t));
}

const amp_element_interface_t *amp_sine_pcm_reader_get_element_interface() { return &amp_sine_pcm_element_interface; }
