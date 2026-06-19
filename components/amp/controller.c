#include <assert.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/ringbuf.h"

#include "amp/controller.h"
#include "amp/i2s_writer.h"
#include "amp/sin_pcm_reader.h"
#include "element_priv.h"
#include "utils/esp_utils.h"

#define DEFAULT_FRAMES_SIZE 1024

static const char *TAG = "amp_controller";

ESP_EVENT_DEFINE_BASE(AMP_EVENT_ACTION);

ESP_EVENT_DEFINE_BASE(AMP_EVENT_REPORT);

// ################# ringbuf list ##############

typedef struct {
    size_t size, cap;
    RingbufHandle_t *items;
} rb_list_t;

static inline void rb_list_init(rb_list_t *rb) {
    rb->size = rb->cap = 0;
    rb->items = NULL;
}

static inline void rb_list_deinit(rb_list_t *rb) {
    if (rb->items) {
        free(rb->items);
        rb->items = NULL;
    }
}

static inline esp_err_t rb_list_realloc(rb_list_t *rb, size_t require_size) {
    if (rb->cap < require_size) {
        size_t new_size = rb->cap > 0 ? rb->cap << 1 : 2; /* double caps */
        if (new_size < require_size) {
            new_size = require_size;
        }
        RingbufHandle_t *items = realloc(rb->items, new_size * sizeof(RingbufHandle_t));
        if (!items) {
            return ESP_ERR_NO_MEM;
        }
        rb->items = items;
        rb->cap = new_size;
    }
    return ESP_OK;
}

static inline esp_err_t rb_list_append(rb_list_t *rb_list, RingbufHandle_t rb) {
    esp_err_t err = rb_list_realloc(rb_list, rb_list->size + 1);
    if (ESP_OK != err) {
        return err; // no memory
    }
    rb_list->items[rb_list->size] = rb;
    rb_list->size++;
    return ESP_OK;
}

static inline RingbufHandle_t rb_list_at(rb_list_t *rb, int idx) {
    if (idx < 0 || idx >= rb->size) {
        return NULL;
    }
    return rb->items[idx];
}

static inline RingbufHandle_t rb_list_last(rb_list_t *rb) { return rb_list_at(rb, rb->size - 1); }

static inline RingbufHandle_t rb_list_first(rb_list_t *rb) { return rb_list_at(rb, 0); }

typedef STAILQ_HEAD(amp_el_head, amp_element) amp_element_list_head_t;

struct amp_controller {
    TaskHandle_t self;
    esp_event_loop_handle_t event_bus;
    amp_element_list_head_t el_list;
    amp_dashboard_handle_t *dashboard;
    rb_list_t rb_list;
};

static esp_err_t inline element_task_run(amp_element_handle_t *el) {
    if (el->intf && el->intf->task_run) {
        TaskHandle_t t;
        BaseType_t ret = xTaskCreate((el->intf->task_run), el->name, el->stack_size, (void *)el, 1, &t);
        if (ret == pdTRUE) {
            el->task = t;
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static inline esp_err_t amp_controller_append(amp_controller_handle_t *controller, amp_element_handle_t *el,
                                              const amp_element_interface_t *intf,
                                              const struct amp_element_task_cfg *cfg) {
    assert(el && intf);
    // setup
    el->name = strdup(cfg->name);
    el->stack_size = cfg->stack_size;
    el->intf = intf;
    el->dashboard = controller->dashboard;
    el->task = NULL;
    el->event_bus = controller->event_bus;

    RingbufHandle_t rb;
    bool append_rb = false;
    switch (el->role) {
    case AMP_ELEMENT_READER:
        assert(intf->set_output_rb);
        // set output
        rb = xRingbufferCreate(cfg->rb_out_size, RINGBUF_TYPE_BYTEBUF);
        intf->set_output_rb(el, rb);
        append_rb = true;
        break;
    case AMP_ELEMENT_PROCESSOR:
        assert(intf->set_input_rb && intf->set_output_rb);
        // 1. link input rb
        rb = rb_list_last(&controller->rb_list);
        if (rb == NULL) {
            ESP_LOGE(TAG, "no available ringbuffer");
            return ESP_ERR_INVALID_STATE;
        }
        intf->set_input_rb(el, rb);
        // 2. set output rb
        rb = xRingbufferCreate(cfg->rb_out_size, RINGBUF_TYPE_BYTEBUF);
        intf->set_output_rb(el, rb);
        append_rb = true;
        break;
    case AMP_ELEMENT_WRITER:
        assert(intf->set_input_rb);
        rb = rb_list_last(&controller->rb_list);
        if (rb == NULL) {
            ESP_LOGE(TAG, "no available ringbuffer");
            return ESP_ERR_INVALID_STATE;
        }
        intf->set_input_rb(el, rb);
        break;
    default:
        ESP_LOGE(TAG, "amp element role(%d) is invalid", el->role);
        exit(1);
    }
    STAILQ_INSERT_TAIL(&(controller->el_list), el, stailq_entry);
    esp_err_t err = ESP_OK;
    if (append_rb) {
        err = rb_list_append(&controller->rb_list, rb);
    }
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "append ringbuf fail: %s", esp_err_to_name(err));
    }
    // setup event handler
    if (el->intf && el->intf->setup_event_handler) {
        err = el->intf->setup_event_handler(el, controller->event_bus);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "setup %s event handler fail: %s", el->name, esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "setup %s event handler success", el->name);
    }
    return err;
}

static inline esp_err_t amp_controller_setup_event(amp_controller_handle_t *controller) {
    esp_event_loop_handle_t event_loop;
    esp_event_loop_args_t args = {
        .queue_size = 16,
        .task_stack_size = 4096,
        .task_core_id = tskNO_AFFINITY,
        .task_name = "amp_event_loop",
        .task_priority = 1,
    };
    esp_err_t err = esp_event_loop_create(&args, &event_loop);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "create esp event loop fail: %s", esp_err_to_name(err));
        return err;
    }
    controller->event_bus = event_loop;
    return ESP_OK;
}

esp_err_t amp_controller_init(amp_controller_handle_t **controller) {
    amp_controller_handle_t *c = malloc(sizeof(amp_controller_handle_t));
    STAILQ_INIT(&(c->el_list));
    rb_list_init(&(c->rb_list));
    esp_err_t err = amp_controller_setup_event(c);
    if (ESP_OK != err) {
        goto cleanup;
    }
    amp_dashboard_handle_t *dashboard;
    err = amp_dashboard_init(&dashboard);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "initialize amp dashboard fail: %s", esp_err_to_name(err));
        goto cleanup;
    }
    c->dashboard = dashboard;

    *controller = c;
    return ESP_OK;
cleanup:
    if (dashboard)
        amp_dashboard_deinit(dashboard);
    if (c) {
        if (c->event_bus)
            esp_event_loop_delete(c->event_bus);
        free(c);
    }
    return err;
}

void amp_controller_deinit(amp_controller_handle_t *controller) {
    if (!controller)
        return;
    amp_element_handle_t *el;
    STAILQ_FOREACH(el, &controller->el_list, stailq_entry) {
        if (el->intf && el->intf->deinit)
            el->intf->deinit(el);
    }
    for (size_t i = 0; i < (controller->rb_list).size; i++) {
        RingbufHandle_t rb = controller->rb_list.items[i];
        if (rb)
            vRingbufferDelete(rb);
    }
    rb_list_deinit(&controller->rb_list);
    free(controller);
}

esp_err_t amp_controller_append_reader(amp_controller_handle_t *controller, amp_element_handle_t *el,
                                       const amp_element_interface_t *intf, const struct amp_element_task_cfg *cfg) {
    el->role = AMP_ELEMENT_READER;
    return amp_controller_append(controller, el, intf, cfg);
}

esp_err_t amp_controller_append_writer(amp_controller_handle_t *controller, amp_element_handle_t *el,
                                       const amp_element_interface_t *intf, const struct amp_element_task_cfg *cfg) {
    el->role = AMP_ELEMENT_WRITER;
    return amp_controller_append(controller, el, intf, cfg);
}

esp_err_t amp_controller_run(amp_controller_handle_t *controller) {
    amp_element_handle_t *el;
    esp_err_t err;
    // start all element
    STAILQ_FOREACH(el, &controller->el_list, stailq_entry) {
        err = element_task_run(el);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "create element %s task fail", el->name);
            return err;
        }
        ESP_LOGI(TAG, "create element %s task success", el->name);
    }
    return ESP_OK;
}

esp_err_t amp_controller_pause(amp_controller_handle_t *controller) {
    amp_dashboard_handle_t *dashboard = controller->dashboard;
    enum amp_state old = amp_dashboard_swap_status(dashboard, AMP_STATE_PAUSE);
    if (old == AMP_STATE_PAUSE) {
        ESP_LOGW(TAG, "amp already pause state");
        return ESP_OK;
    }
    // send event
    esp_event_loop_handle_t event_bus = controller->event_bus;
    esp_err_t err = esp_event_post_to(event_bus, AMP_EVENT_ACTION, AMP_EVENT_ACTION_PAUSE, 0, 0, portMAX_DELAY);
    return err;
}
