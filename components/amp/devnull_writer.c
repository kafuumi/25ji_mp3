
#include "amp/devnull_writer.h"

#include "amp/amp_mem.h"
#include "element_priv.h"
#include "esp_log.h"

static const char *TAG = "devnull_writer";

struct devnull_writer {
    AMP_ELEMENT_ENTRY() el_entry;
    ringbuf_handle_t rb_in;
};

static bool devnull_writer_do_event(devnull_writer_handle_t writer, TickType_t wait_time) {
    uint32_t notify = 0;
    if (xTaskNotifyWait(0, ULONG_MAX, &notify, wait_time) == pdTRUE) {
        ESP_LOGI(TAG, "receive event notify: %lu", notify);
    }
    enum amp_state state = amp_dashboard_load_state(writer->el_entry.dashboard);
    return state == AMP_STATE_PLAYING;
}

static void devnull_writer_task(void *args) {
    devnull_writer_handle_t writer = args;
    ringbuf_handle_t rb = writer->rb_in;
    assert(rb);

    const TickType_t max_wait = pdMS_TO_TICKS(1000);
    TickType_t notify_wait = 0;
    const int read_size = 1024;

    while (true) {
        if (!devnull_writer_do_event(writer, notify_wait)) {
            notify_wait = pdMS_TO_TICKS(100);
            continue;
        }
        notify_wait = 0;

        int consumed = rb_read(rb, NULL, read_size, max_wait);
        if (consumed == RB_DONE) {
            amp_dashboard_send_done(writer->el_entry.dashboard);
            continue;
        }
        if (consumed < 0) {
            ESP_LOGW(TAG, "ringbuf is empty, no item is received");
        }
    }
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
