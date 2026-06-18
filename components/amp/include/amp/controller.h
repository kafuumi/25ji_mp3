#if !defined(_AMP_CONTROLLER_H_)
#define _AMP_CONTROLLER_H_

#include "esp_err.h"
#include "freertos/ringbuf.h"

#define AMP_ELEMENT_READER()                                                                                           \
    struct {                                                                                                           \
        size_t task_size;                                                                                              \
        RingbufferType_t rb_out;                                                                                       \
    }

struct amp_element_interface {
    void (*task_run)(void *);
};

struct amp_element_task_cfg {
    const char *name;
    const int stack_size;
};

typedef struct amp_element amp_element_handle_t;

typedef struct amp_controller amp_controller_handle_t;

typedef struct amp_element_interface amp_element_interface_t;

amp_element_handle_t *amp_element_setup(void *obj, const amp_element_interface_t *intf,
                                        const struct amp_element_task_cfg *cfg);

esp_err_t amp_controller_run(amp_controller_handle_t *controller);

esp_err_t amp_controller_init(amp_controller_handle_t **controller);

void amp_controller_append(amp_controller_handle_t *controller, amp_element_handle_t *el, RingbufHandle_t rb) ;

#endif // _AMP_CONTROLLER_H_
