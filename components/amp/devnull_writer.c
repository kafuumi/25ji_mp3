
#include "amp/devnull_writer.h"

#include "amp/amp_mem.h"
#include "element_priv.h"
#include "esp_log.h"

#define MAX_WAIT_TIME_EVENT pdMS_TO_TICKS(500)

static const char *TAG = "devnull_writer";

struct devnull_writer {
    AMP_ELEMENT_ENTRY() el_entry;
    ringbuf_handle_t rb_in;
};

struct devnull_writer_task_state {
    enum amp_state state;
    TickType_t wait_event_time;
    bool stop_task;
    bool wait_eos_done;
};

static bool devnull_writer_receive_event(devnull_writer_handle_t writer, struct devnull_writer_task_state *task_state) {
    uint32_t notify = 0;
    if (xTaskNotifyWait(0, ULONG_MAX, &notify, task_state->wait_event_time) == pdTRUE) {
        ESP_LOGI(TAG, "receive event notify: %lu", notify);
        if (notify & NOTIFY_VALUE_MASK_STATE) {
            task_state->state = AMP_DASH_LOAD_STATE(writer->el_entry.dashboard);
        }
        if (notify & NOTIFY_VALUE_MASK_EOS_DONE) {
            task_state->wait_eos_done = false;
        }
    }
    bool notplay;
    if (task_state->wait_eos_done) {
        notplay = true;
    } else {
        notplay = task_state->state != AMP_STATE_PLAYING;
    }

    if (notplay) {
        if (task_state->wait_event_time <= 0) {
            task_state->wait_event_time = MAX_WAIT_TIME_EVENT;
        }
    } else if (task_state->wait_event_time > 0) {
        task_state->wait_event_time = 0;
    }
    return notplay;
}

static void devnull_writer_task(void *args) {
    devnull_writer_handle_t writer = args;
    ringbuf_handle_t rb = writer->rb_in;
    assert(rb);

    struct devnull_writer_task_state task_state = {
        .state = AMP_DASH_LOAD_STATE(writer->el_entry.dashboard),
        .wait_event_time = MAX_WAIT_TIME_EVENT,
        .stop_task = false,
        .wait_eos_done = false,
    };
    TickType_t wait_read_time = pdMS_TO_TICKS(1000);
    const int read_size = 1024;

    while (true) {
        if (task_state.stop_task) {
            break;
        }
        if (devnull_writer_receive_event(writer, &task_state)) {
            continue;
        }
        // mock
        vTaskDelay(pdMS_TO_TICKS(10));
        int consumed = rb_read(rb, NULL, read_size, wait_read_time);
        if (RB_DONE == consumed) {
            if (!task_state.wait_eos_done) {
                ESP_LOGW(TAG, "input ringbuf is done write");
                AMP_EL_SEND_DONE(TAG, writer, el_entry);
                task_state.wait_eos_done = true;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            continue;
        } else if (RB_ABORT == consumed) {
            ESP_LOGW(TAG, "input ringbuf is abort read");
            continue;
        } else if (RB_TIMEOUT == consumed) {
            ESP_LOGW(TAG, "read data from input ringbuf timeout");
            continue;
        } else if (consumed <= 0) {
            ESP_LOGW(TAG, "read data from input ringbuf fail");
            continue;
        } else {
            ESP_LOGD(TAG, "read input ringbuf success, size: %d", consumed);
        }
    }

    vTaskDelete(NULL);
}

static void devnull_writer_set_input(void *args, ringbuf_handle_t rb) {
    devnull_writer_handle_t writer = args;
    writer->rb_in = rb;
}

static void devnull_writer_element_deinit(void *args) { devnull_writer_deinit((devnull_writer_handle_t)args); }

static const amp_element_interface_t devnull_writer_element_interface = {
    .deinit = devnull_writer_element_deinit,
    .task_run = devnull_writer_task,
    .set_input_rb = devnull_writer_set_input,
    .set_output_rb = NULL,
    .setup_event_handler = NULL,
};

esp_err_t devnull_writer_init(devnull_writer_handle_t *writer) {
    if (!writer) {
        return ESP_ERR_INVALID_ARG;
    }

    devnull_writer_handle_t w = amp_calloc(1, sizeof(struct devnull_writer));
    if (!w) {
        return ESP_ERR_NO_MEM;
    }

    *writer = w;
    return ESP_OK;
}

void devnull_writer_deinit(devnull_writer_handle_t writer) {
    if (!writer) {
        return;
    }
    amp_free(writer);
}

const amp_element_interface_t *devnull_writer_el_interface() { return &devnull_writer_element_interface; }
