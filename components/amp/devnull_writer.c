
#include "amp/devnull_writer.h"

#include "amp/amp_mem.h"
#include "amp/element.h"
#include "dashboard.h"
#include "element_priv.h"
#include "esp_log.h"
#include "freertos/projdefs.h"

#define AMP_DEVNULL_WRITER_EVENT_WAIT_TICKS pdMS_TO_TICKS(500)

static const char *TAG = "devnull_writer";

struct devnull_writer {
    AMP_ELEMENT_ENTRY() el_entry;
    ringbuf_handle_t rb_in;
};

typedef enum {
    DW_STATE_PLAYING,
    DW_STATE_WAIT_NOTIFY,
} amp_devnull_writer_state_t;

typedef struct {
    TickType_t event_wait_ticks;
    bool stopped;
    amp_devnull_writer_state_t state;
} amp_devnull_writer_task_state_t;

static bool amp_devnull_writer_process_notify(amp_devnull_writer_handle_t writer,
                                              amp_devnull_writer_task_state_t *state) {
    uint32_t notify = 0;
    EL_WAIT_NOTIFY(notify, state->event_wait_ticks) {
        EL_NOTIFY_ON_STOP(notify) {
            state->stopped = true;
            return true;
        }
        state->state = AMP_DASH_IS_PLAYING(writer->el_entry.dashboard) ? DW_STATE_PLAYING : DW_STATE_WAIT_NOTIFY;
        EL_NOTIFY_ON_STREAM_NEW(notify) { ESP_LOGI(TAG, "receive STREAM NEW notify"); }
    }
    bool should_wait = state->state == DW_STATE_WAIT_NOTIFY;
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
        .event_wait_ticks = AMP_DEVNULL_WRITER_EVENT_WAIT_TICKS,
        .stopped = false,
        .state = AMP_DASH_IS_PLAYING(writer->el_entry.dashboard) ? DW_STATE_PLAYING : DW_STATE_WAIT_NOTIFY,
    };
    TickType_t read_wait_ticks = pdMS_TO_TICKS(1000);
    const int read_size = 1024;

    while (true) {
        if (task_state.stopped) {
            break;
        }
        if (amp_devnull_writer_process_notify(writer, &task_state)) {
            continue;
        }
        int consumed = rb_read(rb, NULL, read_size, read_wait_ticks);
        if (RB_DONE == consumed) {
            ESP_LOGI(TAG, "input ringbuf done");
            task_state.state = DW_STATE_WAIT_NOTIFY;
            amp_element_notify_event((amp_element_handle_t)writer, NOTIFY_VALUE_MASK_STREAM_END);
            continue;
        } else if (RB_ABORT == consumed) {
            ESP_LOGW(TAG, "input ringbuf aborted");
            task_state.state = DW_STATE_WAIT_NOTIFY;
            amp_element_notify_event((amp_element_handle_t)writer, NOTIFY_VALUE_MASK_STREAM_ABORT);
            continue;
        } else if (RB_TIMEOUT == consumed) {
            ESP_LOGW(TAG, "read data from input ringbuf timeout");
            continue;
        } else if (RB_UNBLOCK == consumed) {
            ESP_LOGI(TAG, "write ringbuf unblock, drop data, written: %d", consumed);
        } else if (consumed <= 0) {
            ESP_LOGW(TAG, "read data from input ringbuf fail");
            continue;
        } else {
            ESP_LOGD(TAG, "read from ringbuf: %d bytes", consumed);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    amp_element_task_done((amp_element_handle_t)writer);
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
