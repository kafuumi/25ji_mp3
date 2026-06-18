
#include "esp_log.h"
#include <sys/queue.h>

#include "amp/controller.h"
#include "amp/i2s_writer.h"
#include "amp/sin_pcm_reader.h"
#include "utils/esp_utils.h"

#define DEFAULT_FRAMES_SIZE 1024

static const char *TAG = "amp_controller";

#define RB_LIST()                                                                                                      \
    struct {                                                                                                           \
        size_t size, cap;                                                                                              \
        RingbufHandle_t *items;                                                                                        \
    }

#define RB_LIST_GROW(rb, size)                                                                                         \
    size_t _rs = size;                                                                                                 \
    if ((rb)->cap < _rs) {                                                                                             \
        size_t _ns = (rb)->cap << 1; /* double caps*/                                                                  \
        if (_ns < _rs)                                                                                                 \
            _ns = _rs;                                                                                                 \
        (rb)->items = realloc((rb)->items, _ns * sizeof(RingbufHandle_t));                                             \
        (rb)->cap = _ns;                                                                                               \
    }

#define RB_LIST_APPEND(list, rb)                                                                                       \
    RB_LIST_GROW(list, ((list)->size + 1));                                                                            \
    (list)->items[((list)->size)] = rb;                                                                                \
    (list)->size++;

#define RB_LIST_AT(list, idx, rb)                                                                                      \
    if (idx < = 0 || idx >= (list)->size)                                                                              \
        rb = NULL;                                                                                                     \
    else                                                                                                               \
        rb = (list)->items[idx];

struct amp_element {
    char *name;
    void *obj;
    int stack_size;
    TaskHandle_t task;
    STAILQ_ENTRY(amp_element) entry;

    amp_element_interface_t *intf;
};

STAILQ_HEAD(amp_el_head, amp_element);

enum amp_state { AMP_STATE_INIT, AMP_STATE_READY };

struct amp_controller {
    enum amp_state state;
    struct amp_el_head el_list;
    RB_LIST() rb_list;
};

static esp_err_t element_task_run(amp_element_handle_t *el) {
    TaskHandle_t t;
    BaseType_t ret = xTaskCreate((el->intf->task_run), el->name, el->stack_size, (void *)el->obj, 1, &t);
    if (ret == pdTRUE) {
        el->task = t;
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t amp_controller_init(amp_controller_handle_t **controller) {
    amp_controller_handle_t *c = malloc(sizeof(amp_controller_handle_t));
    STAILQ_INIT(&(c->el_list));
    memset(&(c->rb_list), 0, sizeof(c->rb_list));
    *controller = c;
    return ESP_OK;
}

void amp_controller_deinit(amp_controller_handle_t *controller) {
    if (controller)
        free(controller);
}

amp_element_handle_t *amp_element_setup(void *obj, const amp_element_interface_t *intf,
                                        const struct amp_element_task_cfg *cfg) {
    amp_element_handle_t *el = malloc(sizeof(amp_element_handle_t));
    el->name = strdup(cfg->name);
    el->stack_size = cfg->stack_size;
    el->obj = obj;
    el->intf = intf;
    return el;
}

void amp_controller_append(amp_controller_handle_t *controller, amp_element_handle_t *el, RingbufHandle_t rb) {
    STAILQ_INSERT_TAIL(&(controller->el_list), el, entry);
    if (rb) {
        RB_LIST_APPEND(&controller->rb_list, rb);
    }
}

esp_err_t amp_controller_run(amp_controller_handle_t *controller) {
    amp_element_handle_t *el;
    esp_err_t err;
    STAILQ_FOREACH(el, &controller->el_list, entry) {
        err = element_task_run(el);
        if (ESP_OK != err) {
            ESP_LOGE(TAG, "create element %s task fail", el->name);
            return err;
        }
        ESP_LOGI(TAG, "create element %s task success", el->name);
    }
    return ESP_OK;
}
