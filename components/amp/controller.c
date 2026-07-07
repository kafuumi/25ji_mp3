#include <assert.h>

#include "amp/element.h"
#include "dashboard.h"
#include "esp_err.h"
#include "esp_log.h"

#include "amp/amp_event.h"
#include "amp/amp_mem.h"
#include "amp/controller.h"
#include "amp/ringbuf.h"
#include "element_priv.h"
#include "utils/esp_utils.h"

#define DEFAULT_FRAMES_SIZE 1024

#define CONTROLLER_ACTION_DO(controller, state, action, tag, log_fmt, ...)                                             \
    do {                                                                                                               \
        enum amp_state old = AMP_DASH_SWAP_STATE((controller)->dashboard, state);                                      \
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

////////////////////////////////////////////////////////

typedef STAILQ_HEAD(amp_el_head, amp_element) amp_element_list_head_t;

struct amp_controller {
    TaskHandle_t self;
    esp_event_loop_handle_t event_bus;
    esp_event_handler_instance_t report_evt;
    amp_dashboard_handle_t dashboard;
    int el_size;
    amp_element_list_head_t el_list;
    rb_list_t rb_list;
};

static inline esp_err_t amp_controller_append(amp_controller_handle_t controller, amp_element_handle_t el,
                                              const amp_element_task_config_t *cfg) {
    const amp_element_interface_t *intf = cfg->intf;
    assert(el && intf);
    // setup
    el->name = strdup(cfg->name);
    el->stack_size = cfg->stack_size;
    el->affinity_core = cfg->affinity_core;
    el->task_priority = cfg->task_priority;
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
        rb = rb_create(sizeof(uint8_t), cfg->output_rb_size);
        intf->set_output_rb(el, rb);
        append_rb = true;
        break;
    case AMP_ELEMENT_PROCESSOR:
        assert(intf->set_input_rb && intf->set_output_rb);
        // 1. link input rb
        rb = rb_list_last(&controller->rb_list);
        if (rb == NULL) {
            ESP_LOGE(TAG, "no ringbuffer available in pipeline");
            return ESP_ERR_INVALID_STATE;
        }
        intf->set_input_rb(el, rb);
        // 2. set output rb
        rb = rb_create(sizeof(uint8_t), cfg->output_rb_size);
        intf->set_output_rb(el, rb);
        append_rb = true;
        break;
    case AMP_ELEMENT_WRITER:
        assert(intf->set_input_rb);
        rb = rb_list_last(&controller->rb_list);
        if (rb == NULL) {
            ESP_LOGE(TAG, "no ringbuffer available in pipeline");
            return ESP_ERR_INVALID_STATE;
        }
        intf->set_input_rb(el, rb);
        break;
    default:
        ESP_LOGE(TAG, "invalid element role: %d", el->role);
        abort();
    }
    STAILQ_INSERT_TAIL(&(controller->el_list), el, stailq_entry);
    esp_err_t err = ESP_OK;
    if (append_rb) {
        err = rb_list_append(&controller->rb_list, rb);
    }
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "failed to append ringbuf: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t amp_controller_append_reader(amp_controller_handle_t controller, amp_element_handle_t el,
                                       const amp_element_task_config_t *cfg) {
    el->role = AMP_ELEMENT_READER;
    return amp_controller_append(controller, el, cfg);
}

esp_err_t amp_controller_append_writer(amp_controller_handle_t controller, amp_element_handle_t el,
                                       const amp_element_task_config_t *cfg) {
    el->role = AMP_ELEMENT_WRITER;
    return amp_controller_append(controller, el, cfg);
}

esp_err_t amp_controller_append_processor(amp_controller_handle_t controller, amp_element_handle_t el,
                                          const amp_element_task_config_t *cfg) {
    el->role = AMP_ELEMENT_PROCESSOR;
    return amp_controller_append(controller, el, cfg);
}

static esp_err_t inline element_task_run(amp_element_handle_t el) {
    if (el->intf && el->intf->run_task) {
        TaskHandle_t t;
        BaseType_t ret;
        if (el->affinity_core >= 0) {
            ret = xTaskCreatePinnedToCore((el->intf->run_task), el->name, el->stack_size, (void *)el, el->task_priority,
                                          &t, el->affinity_core);
        } else {
            ret = xTaskCreate((el->intf->run_task), el->name, el->stack_size, (void *)el, el->task_priority, &t);
        }
        if (ret == pdTRUE) {
            el->task = t;
            return ESP_OK;
        }
    }
    return ESP_FAIL;
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
        ESP_LOGE(TAG, "failed to create event loop: %s", esp_err_to_name(err));
        return err;
    }
    controller->event_bus = event_loop;
    return ESP_OK;
}

static void amp_controller_task_run(void *args) {
    amp_controller_handle_t controller = args;
    while (true) {
        uint32_t notify;
        if (xTaskNotifyWait(0, ULONG_MAX, &notify, portMAX_DELAY) != pdTRUE) {
            // sleep to wait
            ESP_LOGI(TAG, "controller task waiting for notify");
            continue;
        }
        EL_NOTIFY_ON_WHAT(notify, NOTIFY_VALUE_MASK_STREAM_END) {
            ESP_LOGI(TAG, "received STREAM END event");
            // reset ringbuf
            ESP_LOGI(TAG, "all elements done, resetting ringbufs");
            rb_list_t *rb_list = &controller->rb_list;
            for (int i = 0; i < rb_list->size; ++i) {
                rb_reset_is_done_write(rb_list->items[i]);
            }
            /* send eos done to element */
            amp_element_handle_t el;
            STAILQ_FOREACH(el, &controller->el_list, stailq_entry) {
                xTaskNotify(el->task, NOTIFY_VALUE_MASK_STREAM_NEW, eSetBits);
            }
        }
        EL_NOTIFY_ON_STREAM_ABORT(notify) {
            ESP_LOGI(TAG, "received STREAM ABORT event");
            // reset ringbuf
            ESP_LOGI(TAG, "all elements done, resetting ringbufs");
            rb_list_t *rb_list = &controller->rb_list;
            for (int i = 0; i < rb_list->size; ++i) {
                rb_reset(rb_list->items[i]);
            }
            /* send STREAM NEW to element */
            amp_element_handle_t el;
            STAILQ_FOREACH(el, &controller->el_list, stailq_entry) {
                xTaskNotify(el->task, NOTIFY_VALUE_MASK_STREAM_NEW, eSetBits);
            }
        }
    }
}

esp_err_t amp_controller_run(amp_controller_handle_t controller) {
    if (controller->self) {
        ESP_LOGE(TAG, "amp controller already running");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err;
    TaskHandle_t self;
    if (xTaskCreate(amp_controller_task_run, "controller", 4096, controller, 10, &self) != pdTRUE) {
        return ESP_FAIL;
    }
    controller->self = self;
    int size = 0;
    // start all element
    amp_element_handle_t el;
    STAILQ_FOREACH(el, &controller->el_list, stailq_entry) {
        el->controller_task = self;
        err = element_task_run(el);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "failed to create task for %s", el->name);
            return err;
        }
        ESP_LOGI(TAG, "created task for %s", el->name);
        size++;
    }
    if ((controller->dashboard->done_count = xSemaphoreCreateCounting(size, 0)) == NULL) {
        return ESP_FAIL;
    }
    controller->el_size = size;
    return ESP_OK;
}

////////////////////////////////////////////////////////////////////////////////////

enum amp_controller_action_id {
    AMP_CONTROLLER_ACTION_PLAY,
    AMP_CONTROLLER_ACTION_PAUSE,
    AMP_CONTROLLER_ACTION_RESET,
    AMP_CONTROLLER_ACTION_NEXT,
    AMP_CONTROLLER_ACTION_PREV,
};

static inline void amp_controller_send_notify(amp_controller_handle_t controller, uint32_t notify) {
    amp_element_handle_t el;
    STAILQ_FOREACH(el, &controller->el_list, stailq_entry) {
        if (el && el->task) {
            if (xTaskNotify(el->task, notify, eSetBits) != pdTRUE) {
                ESP_LOGW(TAG, "failed to notify %s of state change", el->name);
            }
        }
    }
}

static inline esp_err_t amp_controller_send_action_event(amp_controller_handle_t controller,
                                                         enum amp_controller_action_id evt) {
    // send event by task notify
    enum amp_state state;
    switch (evt) {
    case AMP_CONTROLLER_ACTION_NEXT:
    case AMP_CONTROLLER_ACTION_PREV:
    case AMP_CONTROLLER_ACTION_PAUSE:
        state = AMP_STATE_PAUSE;
        break;
    case AMP_CONTROLLER_ACTION_PLAY:
        state = AMP_STATE_PLAYING;
        break;
    case AMP_CONTROLLER_ACTION_RESET:
        state = AMP_STATE_READY;
        break;
    default:
        return ESP_OK;
    }
    AMP_DASH_SWAP_STATE(controller->dashboard, state);

    amp_controller_send_notify(controller, NOTIFY_VALUE_MASK_STATE);
    return ESP_OK;
}

esp_err_t amp_controller_action_next(amp_controller_handle_t controller) {
    amp_element_handle_t el;
    STAILQ_FOREACH(el, &(controller->el_list), stailq_entry) {
        if (el && el->role == AMP_ELEMENT_READER) {
            if (xTaskNotify(el->task, NOTIFY_VALUE_MASK_STREAM_ABORT, eSetBits) != pdTRUE) {
                ESP_LOGW(TAG, "failed to notify %s of state change", el->name);
            }
        }
    }
    // abort all
    for (size_t i = 0; i < (controller->rb_list).size; ++i) {
        ringbuf_handle_t rb = controller->rb_list.items[i];
        if (rb) {
            rb_abort(rb);
        }
    }
    return ESP_OK;
}

esp_err_t amp_controller_action_reset(amp_controller_handle_t controller) {
    CONTROLLER_ACTION_DO(controller, AMP_STATE_READY, AMP_CONTROLLER_ACTION_RESET, TAG, "amp already READY state");
}

esp_err_t amp_controller_action_play(amp_controller_handle_t controller) {
    CONTROLLER_ACTION_DO(controller, AMP_STATE_PLAYING, AMP_CONTROLLER_ACTION_PLAY, TAG, "amp already PLAYING state");
}

esp_err_t amp_controller_action_pause(amp_controller_handle_t controller) {
    CONTROLLER_ACTION_DO(controller, AMP_STATE_PAUSE, AMP_CONTROLLER_ACTION_PAUSE, TAG, "amp already PAUSED state");
    // unblock write
    for (size_t i = 0; i < (controller->rb_list).size; ++i) {
        ringbuf_handle_t rb = controller->rb_list.items[i];
        if (rb) {
            rb_unblock_writer(rb);
        }
    }
}

esp_err_t amp_controller_action_toggle_play(amp_controller_handle_t controller, bool *to_play) {
    enum amp_state state = AMP_DASH_LOAD_STATE(controller->dashboard);
    if (state == AMP_STATE_PAUSE || state == AMP_STATE_READY) {
        if (to_play)
            *to_play = true;
        return amp_controller_action_play(controller);
    } else if (state == AMP_STATE_PLAYING) {
        if (to_play)
            *to_play = false;
        return amp_controller_action_pause(controller);
    } else if (state == AMP_STATE_FATAL) {
        ESP_LOGW(TAG, "state is FATAL, reset required");
        return ESP_ERR_INVALID_STATE;
    } else {
        ESP_LOGW(TAG, "invalid state: %d", state);
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t amp_controller_init(amp_controller_handle_t *controller) {
    amp_controller_handle_t c = amp_calloc(1, sizeof(struct amp_controller));
    STAILQ_INIT(&(c->el_list));
    rb_list_init(&(c->rb_list));
    esp_err_t err = amp_controller_setup_event(c);
    if (ESP_OK != err) {
        goto cleanup;
    }
    amp_dashboard_handle_t dashboard;
    err = amp_dashboard_init(&dashboard);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "failed to initialize dashboard: %s", esp_err_to_name(err));
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

    if (controller->dashboard) {
        amp_dashboard_deinit(controller->dashboard);
    }
    if (controller->event_bus) {
        esp_event_loop_delete(controller->event_bus);
    }

    amp_free(controller);
}
