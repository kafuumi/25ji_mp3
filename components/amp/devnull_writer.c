
#include "amp/devnull_writer.h"

#include "amp/amp_mem.h"
#include "element_priv.h"
#include "esp_log.h"

#define AMP_DEVNULL_WRITER_EVENT_WAIT_TICKS pdMS_TO_TICKS(500)

static const char *TAG = "devnull_writer";

struct devnull_writer {
    AMP_ELEMENT_ENTRY() el_entry;
    ringbuf_handle_t rb_in;
};

typedef struct {
    enum amp_state cached_state;
    TickType_t event_wait_ticks;
    bool stop_requested;
    bool waiting_eos_done;
} amp_devnull_writer_task_state_t;

static bool amp_devnull_writer_process_notify(amp_devnull_writer_handle_t writer,
                                              amp_devnull_writer_task_state_t *state) {
    uint32_t notify = 0;
    if (xTaskNotifyWait(0, ULONG_MAX, &notify, state->event_wait_ticks) == pdTRUE) {
        ESP_LOGD(TAG, "received notify: 0x%lx", notify);
        if (notify & NOTIFY_VALUE_MASK_STATE) {
            state->cached_state = AMP_DASH_LOAD_STATE(writer->el_entry.dashboard);
        }
        if (notify & NOTIFY_VALUE_MASK_EOS_DONE) {
            state->waiting_eos_done = false;
        }
    }
    bool should_wait;
    if (state->waiting_eos_done) {
        should_wait = true;
    } else {
        should_wait = state->cached_state != AMP_STATE_PLAYING;
    }

    if (should_wait) {
        if (state->event_wait_ticks <= 0) {
            state->event_wait_ticks = AMP_DEVNULL_WRITER_EVENT_WAIT_TICKS;
        }
    } else if (state->event_wait_ticks > 0) {
        state->event_wait_ticks = 0;
    }
    return should_wait;
}

static void amp_devnull_writer_task(void *args) {
    amp_devnull_writer_handle_t writer = args;
    ringbuf_handle_t rb = writer->rb_in;
    assert(rb);

    amp_devnull_writer_task_state_t task_state = {
        .cached_state = AMP_DASH_LOAD_STATE(writer->el_entry.dashboard),
        .event_wait_ticks = AMP_DEVNULL_WRITER_EVENT_WAIT_TICKS,
        .stop_requested = false,
        .waiting_eos_done = false,
    };
    TickType_t read_wait_ticks = pdMS_TO_TICKS(1000);
    const int read_size = 1024;

    while (true) {
        if (task_state.stop_requested) {
            break;
        }
        if (amp_devnull_writer_process_notify(writer, &task_state)) {
            continue;
        }
        int consumed = rb_read(rb, NULL, read_size, read_wait_ticks);
        if (RB_DONE == consumed) {
            if (!task_state.waiting_eos_done) {
                ESP_LOGI(TAG, "input ringbuf done");
                AMP_EL_SEND_DONE(TAG, writer, el_entry);
                task_state.waiting_eos_done = true;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            continue;
        } else if (RB_ABORT == consumed) {
            ESP_LOGW(TAG, "input ringbuf aborted");
            continue;
        } else if (RB_TIMEOUT == consumed) {
            ESP_LOGW(TAG, "read data from input ringbuf timeout");
            continue;
        } else if (consumed <= 0) {
            ESP_LOGW(TAG, "read data from input ringbuf fail");
            continue;
        } else {
            ESP_LOGD(TAG, "read from ringbuf: %d bytes", consumed);
        }
    }

    vTaskDelete(NULL);
}

static void amp_devnull_writer_set_input(void *args, ringbuf_handle_t rb) {
    amp_devnull_writer_handle_t writer = args;
    writer->rb_in = rb;
}

static void amp_devnull_writer_element_deinit(void *args) {
    amp_devnull_writer_deinit((amp_devnull_writer_handle_t)args);
}

static const amp_element_interface_t amp_devnull_writer_element_interface = {
    .deinit = amp_devnull_writer_element_deinit,
    .run_task = amp_devnull_writer_task,
    .set_input_rb = amp_devnull_writer_set_input,
    .set_output_rb = NULL,
    .register_events = NULL,
};

esp_err_t amp_devnull_writer_init(amp_devnull_writer_handle_t *writer) {
    if (!writer) {
        return ESP_ERR_INVALID_ARG;
    }

    amp_devnull_writer_handle_t w = amp_calloc(1, sizeof(struct devnull_writer));
    if (!w) {
        return ESP_ERR_NO_MEM;
    }

    *writer = w;
    return ESP_OK;
}

void amp_devnull_writer_deinit(amp_devnull_writer_handle_t writer) {
    if (!writer) {
        return;
    }
    amp_free(writer);
}

const amp_element_interface_t *amp_devnull_writer_get_element_interface(void) {
    return &amp_devnull_writer_element_interface;
}
