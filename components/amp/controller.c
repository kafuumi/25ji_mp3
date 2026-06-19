
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/ringbuf.h"

#include "amp/controller.h"
#include "amp/i2s_writer.h"
#include "amp/sin_pcm_reader.h"
#include "utils/esp_utils.h"

#define DEFAULT_FRAMES_SIZE 1024

static const char *TAG = "amp_controller";

// ################# ringbuf list ##############

typedef struct {
    size_t size, cap;
    RingbufHandle_t *items;
} rb_list_t;

static inline void rb_list_init(rb_list_t *rb) {
    rb->size = rb->cap = 0;
    rb->items = NULL;
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
    amp_element_list_head_t el_list;
    TaskHandle_t self;
    amp_dashboard_handle_t *dashboard;
    rb_list_t rb_list;
};

static esp_err_t element_task_run(amp_element_handle_t *el) {
    TaskHandle_t t;
    BaseType_t ret = xTaskCreate((el->intf->task_run), el->name, el->stack_size, (void *)el, 1, &t);
    if (ret == pdTRUE) {
        el->task = t;
        return ESP_OK;
    }
    return ESP_FAIL;
}

static inline esp_err_t amp_controller_append(amp_controller_handle_t *controller, amp_element_handle_t *el,
                                              const amp_element_interface_t *intf,
                                              const struct amp_element_task_cfg *cfg) {
    // setup
    el->name = strdup(cfg->name);
    el->stack_size = cfg->stack_size;
    el->intf = intf;
    el->dashboard = controller->dashboard;
    el->task = NULL;

    RingbufHandle_t rb;
    bool append_rb = false;
    switch (el->role) {
    case AMP_ELEMENT_READER:
        // set output
        rb = xRingbufferCreate(cfg->rb_out_size, RINGBUF_TYPE_BYTEBUF);
        intf->set_output_rb(el, rb);
        append_rb = true;
        break;
    case AMP_ELEMENT_PROCESSOR:
        break;
    case AMP_ELEMENT_WRITER:
        rb = rb_list_last(&controller->rb_list);
        if (rb == NULL) {
            ESP_LOGE(TAG, "no available ringbuffer");
            return ESP_ERR_INVALID_STATE;
        }
        intf->set_input_rb(el, rb);
        break;
    }
    STAILQ_INSERT_TAIL(&(controller->el_list), el, stailq_entry);
    esp_err_t err = ESP_OK;
    if (append_rb) {
        err = rb_list_append(&controller->rb_list, rb);
    }
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "append ringbuf fail: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t amp_controller_init(amp_controller_handle_t **controller) {
    amp_controller_handle_t *c = malloc(sizeof(amp_controller_handle_t));
    STAILQ_INIT(&(c->el_list));
    rb_list_init(&(c->rb_list));

    amp_dashboard_handle_t *dashboard = NULL;
    c->dashboard = dashboard;

    *controller = c;
    return ESP_OK;
}

void amp_controller_deinit(amp_controller_handle_t *controller) {
    if (controller)
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
