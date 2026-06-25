#include <assert.h>

#include "esp_err.h"
#include "esp_log.h"

#include "amp/amp_mem.h"
#include "amp/controller.h"
#include "amp/i2s_writer.h"
#include "amp/ringbuf.h"
#include "amp/sin_pcm_reader.h"
#include "element_priv.h"
#include "utils/esp_utils.h"

#define DEFAULT_FRAMES_SIZE 1024

#define CONTROLLER_ACTION_DO(controller, state, action, tag, log_fmt, ...)                                             \
    do {                                                                                                               \
        enum amp_state old = amp_dashboard_swap_status((controller)->dashboard, state);                                \
        if (old == state) {                                                                                            \
            ESP_LOGW(tag, log_fmt, ##__VA_ARGS__);                                                                     \
            return ESP_OK;                                                                                             \
        }                                                                                                              \
        return amp_controller_send_action_event((controller), action);                                                 \
    } while (0)

static const char *TAG = "amp_controller";

ESP_EVENT_DEFINE_BASE(AMP_EVENT_ACTION);

ESP_EVENT_DEFINE_BASE(AMP_EVENT_REPORT);

// ################# ringbuf list ##############

typedef struct {
    size_t size, cap;
    ringbuf_handle_t *items;
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
        ringbuf_handle_t *items = amp_realloc(rb->items, new_size * sizeof(ringbuf_handle_t));
        if (!items) {
            return ESP_ERR_NO_MEM;
        }
        rb->items = items;
        rb->cap = new_size;
    }
    return ESP_OK;
}

static inline esp_err_t rb_list_append(rb_list_t *rb_list, ringbuf_handle_t rb) {
    esp_err_t err = rb_list_realloc(rb_list, rb_list->size + 1);
    if (ESP_OK != err) {
        return err; // no memory
    }
    rb_list->items[rb_list->size] = rb;
    rb_list->size++;
    return ESP_OK;
}

static inline ringbuf_handle_t rb_list_at(rb_list_t *rb, int idx) {
    if (idx < 0 || idx >= rb->size) {
        return NULL;
    }
    return rb->items[idx];
}

static inline ringbuf_handle_t rb_list_last(rb_list_t *rb) { return rb_list_at(rb, rb->size - 1); }

static inline ringbuf_handle_t rb_list_first(rb_list_t *rb) { return rb_list_at(rb, 0); }

typedef STAILQ_HEAD(amp_el_head, amp_element) amp_element_list_head_t;

struct amp_controller {
    TaskHandle_t self;
    esp_event_loop_handle_t event_bus;
    esp_event_handler_instance_t report_evt;
    amp_dashboard_handle_t dashboard;
    amp_element_list_head_t el_list;
    rb_list_t rb_list;
};

static esp_err_t inline element_task_run(amp_element_handle_t el) {
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

static void amp_controller_handle_report_event(void *args, esp_event_base_t base_evt, int32_t evt_id, void *evt) {
    // TODO
}

static inline esp_err_t amp_controller_append(amp_controller_handle_t controller, amp_element_handle_t el,
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

    ringbuf_handle_t rb;
    bool append_rb = false;
    switch (el->role) {
    case AMP_ELEMENT_READER:
        assert(intf->set_output_rb);
        // set output
        rb = rb_create(sizeof(uint8_t), cfg->rb_out_size);
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
        rb = rb_create(sizeof(uint8_t), cfg->rb_out_size);
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

static inline esp_err_t amp_controller_setup_event(amp_controller_handle_t controller) {
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
    // register REPORT event handler
    err = esp_event_handler_instance_register_with(event_loop, AMP_EVENT_REPORT, ESP_EVENT_ANY_ID,
                                                   amp_controller_handle_report_event, controller,
                                                   &controller->report_evt);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "register REPORT event handler fail: %s", esp_err_to_name(err));
        esp_event_loop_delete(event_loop);
        return err;
    }
    controller->event_bus = event_loop;
    return ESP_OK;
}

static inline esp_err_t amp_controller_send_action_event(amp_controller_handle_t controller,
                                                         enum amp_event_action_id evt) {
    // send event
    esp_err_t err = esp_event_post_to(controller->event_bus, AMP_EVENT_ACTION, evt, 0, 0, pdMS_TO_TICKS(3000));
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "send action event (%d) fail: %s", evt, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "send action event (%d) success");
    return ESP_OK;
}

esp_err_t amp_controller_init(amp_controller_handle_t *controller) {
    amp_controller_handle_t c = amp_malloc(sizeof(struct amp_controller));
    STAILQ_INIT(&(c->el_list));
    rb_list_init(&(c->rb_list));
    esp_err_t err = amp_controller_setup_event(c);
    if (ESP_OK != err) {
        goto cleanup;
    }
    amp_dashboard_handle_t dashboard;
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

void amp_controller_deinit(amp_controller_handle_t controller) {
    if (!controller)
        return;
    amp_element_handle_t el;
    STAILQ_FOREACH(el, &controller->el_list, stailq_entry) {
        if (el->intf && el->intf->deinit)
            el->intf->deinit(el);
    }
    for (size_t i = 0; i < (controller->rb_list).size; i++) {
        ringbuf_handle_t rb = controller->rb_list.items[i];
        if (rb)
            rb_destroy(rb);
    }
    rb_list_deinit(&controller->rb_list);
    amp_free(controller);
}

esp_err_t amp_controller_append_reader(amp_controller_handle_t controller, amp_element_handle_t el,
                                       const amp_element_interface_t *intf, const struct amp_element_task_cfg *cfg) {
    el->role = AMP_ELEMENT_READER;
    return amp_controller_append(controller, el, intf, cfg);
}

esp_err_t amp_controller_append_writer(amp_controller_handle_t controller, amp_element_handle_t el,
                                       const amp_element_interface_t *intf, const struct amp_element_task_cfg *cfg) {
    el->role = AMP_ELEMENT_WRITER;
    return amp_controller_append(controller, el, intf, cfg);
}

esp_err_t amp_controller_append_processor(amp_controller_handle_t controller, amp_element_handle_t el,
                                          const amp_element_interface_t *intf, const struct amp_element_task_cfg *cfg) {
    el->role = AMP_ELEMENT_PROCESSOR;
    return amp_controller_append(controller, el, intf, cfg);
}

esp_err_t amp_controller_run(amp_controller_handle_t controller) {
    amp_element_handle_t el;
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

esp_err_t amp_controller_action_reset(amp_controller_handle_t controller) {
    CONTROLLER_ACTION_DO(controller, AMP_STATE_READY, AMP_EVENT_ACTION_RESET, TAG, "amp already READY state");
}

esp_err_t amp_controller_action_play(amp_controller_handle_t controller) {
    CONTROLLER_ACTION_DO(controller, AMP_STATE_PLAYING, AMP_EVENT_ACTION_PLAY, TAG, "amp already PLAYING state");
}

esp_err_t amp_controller_action_pause(amp_controller_handle_t controller) {
    CONTROLLER_ACTION_DO(controller, AMP_STATE_PAUSE, AMP_EVENT_ACTION_PAUSE, TAG, "amp already PAUSED state");
}

esp_err_t amp_controller_action_toggle_play(amp_controller_handle_t controller, bool *to_play) {
    enum amp_state state = amp_dashboard_load_state(controller->dashboard);
    if (state == AMP_STATE_PAUSE || state == AMP_STATE_READY) {
        if (to_play)
            *to_play = true;
        return amp_controller_action_play(controller);
    } else if (state == AMP_STATE_PLAYING) {
        if (to_play)
            *to_play = false;
        return amp_controller_action_pause(controller);
    } else if (state == AMP_STATE_FATAL) {
        ESP_LOGW(TAG, "amp state is FATAL, should reset first");
        return ESP_ERR_INVALID_STATE;
    } else {
        ESP_LOGW(TAG, "amp state(%d) is invalid", state);
        return ESP_ERR_INVALID_STATE;
    }
}
