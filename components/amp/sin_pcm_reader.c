
#include <math.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/ringbuf.h"

#include "amp/controller.h"
#include "amp/sin_pcm_reader.h"
#include "element_priv.h"

static const char *TAG = "sin_pcm";

struct sin_pcm_reader {
    AMP_ELEMENT_ENTRY() el_entry;
    int max_amplitude;
    size_t frames_size;
    RingbufHandle_t rb_out;
    struct sin_pcm_audio_args args;
};

static void generate_sin_pcm_16bit(sin_pcm_reader_handle_t *reader, int16_t *buf, float *phase) {
    const struct sin_pcm_audio_args *args = &(reader->args);
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
        if (args->channel == PCM_CHANNEL_STEREO) {
            // write right channel
            *buf = value;
            buf++;
        }
    }
    *phase = tmp;
}

static bool sin_pcm_reader_do_event(sin_pcm_reader_handle_t *reader, TickType_t wait_time) {
    // xxxx-xxxx xxxx-xxxx xxxx-xxxx xxxx-xxxx
    // undefined    self     REPORT   ACTION
    uint32_t notify_count = 0;
    if (xTaskNotifyWait(0, ULONG_MAX, &notify_count, wait_time)) {
        ESP_LOGI(TAG, "receive event notify: %d", notify_count);
    }
    enum amp_state state = amp_dashboard_load_state(reader->el_entry.dashboard);
    return state == AMP_STATE_PLAYING;
}

static void sin_pcm_reader_task(void *args) {
    sin_pcm_reader_handle_t *reader = (sin_pcm_reader_handle_t *)args;
    size_t buf_size = reader->frames_size * reader->args.channel * sizeof(int16_t);
    RingbufHandle_t rb = reader->rb_out;
    assert(rb);

    size_t rb_max_size = xRingbufferGetMaxItemSize(rb);
    if (rb_max_size < buf_size) {
        ESP_LOGE(TAG, "ringbuf max size(%zu) is less than frames_size * channel(%zu)", rb_max_size, buf_size);
        vTaskDelete(NULL);
        return;
    }
    const TickType_t max_wait = pdMS_TO_TICKS(1000);

    float phase = 0;
    void *buf = malloc(buf_size);
    TickType_t wait_time = 0;
    while (true) {
        bool should_gen = sin_pcm_reader_do_event(reader, wait_time);
        if (!should_gen) {
            wait_time = pdMS_TO_TICKS(100);
            continue;
        }
        wait_time = 0;

        // generate pcm data
        {
            generate_sin_pcm_16bit(reader, (int16_t *)buf, &phase);
            BaseType_t ret = xRingbufferSend(rb, buf, buf_size, max_wait);
            if (ret != pdTRUE) {
                ESP_LOGW(TAG, "send ringbuf fail: %d, send size: %zu", ret, buf_size);
                continue;
            }
        }
    }
}

static void sin_pcm_reader_report_event_handler(void *args, esp_event_base_t base_id, int32_t evt_id, void *evt) {
    sin_pcm_reader_handle_t *reader = args;
    uint32_t notify = 0xffffffff;
    switch (evt_id) {
        // todo
    }
    notify &= evt_id << 8;
    if (xTaskNotify(reader->el_entry.task, 0, eIncrement) == pdTRUE) {
        ESP_LOGI(TAG, "send report task notify success, event: %d", evt_id);
    } else {
        ESP_LOGE(TAG, "send report task notify fail");
    }
}

static esp_err_t sin_pcm_reader_register_event(void *args, esp_event_loop_handle_t event_bus) { return ESP_OK; }

static void sin_pcm_reader_set_output(void *args, RingbufHandle_t rb) {
    sin_pcm_reader_handle_t *reader = args;
    reader->rb_out = rb;
}

static const amp_element_interface_t sin_pcm_element_interface = {
    .task_run = sin_pcm_reader_task,
    .set_input_rb = NULL,
    .set_output_rb = sin_pcm_reader_set_output,
    .setup_event_handler = sin_pcm_reader_register_event,
};

// #####################################################################
// ####################### sin_pcm_reader public #######################
// #####################################################################

esp_err_t sin_pcm_reader_init(struct sin_pcm_reader_cfg *cfg, sin_pcm_reader_handle_t **reader) {
    sin_pcm_reader_handle_t *r = malloc(sizeof(sin_pcm_reader_handle_t));
    if (!r) {
        return ESP_ERR_NO_MEM;
    }
    r->frames_size = cfg->frames_size;
    r->max_amplitude = cfg->max_amplitude;
    memset(&(r->args), 0, sizeof(struct sin_pcm_audio_args));
    *reader = r;
    return ESP_OK;
}

void sin_pcm_reader_deinit(sin_pcm_reader_handle_t *reader) {
    if (!reader) {
        return;
    }
    free(reader);
}

void sin_pcm_config_audio(sin_pcm_reader_handle_t *reader, const struct sin_pcm_audio_args *args) {
    memcpy(&(reader->args), args, sizeof(struct sin_pcm_audio_args));
}

amp_element_interface_t *sin_pcm_reader_el_interface() { return (amp_element_interface_t *)&sin_pcm_element_interface; }
