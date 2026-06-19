#if !defined(_AMP_CONTROLLER_H_)
#define _AMP_CONTROLLER_H_

#include "esp_err.h"
#include "freertos/ringbuf.h"
#include <sys/queue.h>

#include "amp/dashboard.h"

#define AMP_ELEMENT_ENTRY() amp_element_handle_t

struct amp_element_interface {
    void (*task_run)(void *);
    void (*set_input_rb)(void *, RingbufHandle_t);
    void (*set_output_rb)(void *, RingbufHandle_t);
};

typedef struct amp_element_interface amp_element_interface_t;

struct amp_element_task_cfg {
    const char *name;
    const int stack_size;
    const size_t rb_out_size;
};

enum amp_element_role {
    AMP_ELEMENT_READER,
    AMP_ELEMENT_PROCESSOR,
    AMP_ELEMENT_WRITER,
};

struct amp_element {
    char *name;
    enum amp_element_role role;
    int stack_size;
    TaskHandle_t task;
    amp_dashboard_handle_t *dashboard;
    STAILQ_ENTRY(amp_element) stailq_entry;

    const amp_element_interface_t *intf;
};

typedef struct amp_element amp_element_handle_t;

typedef struct amp_controller amp_controller_handle_t;

esp_err_t amp_controller_run(amp_controller_handle_t *controller);

esp_err_t amp_controller_init(amp_controller_handle_t **controller);

esp_err_t amp_controller_append_reader(amp_controller_handle_t *controller, amp_element_handle_t *el,
                                       const amp_element_interface_t *intf, const struct amp_element_task_cfg *cfg);

esp_err_t amp_controller_append_writer(amp_controller_handle_t *controller, amp_element_handle_t *el,
                                       const amp_element_interface_t *intf, const struct amp_element_task_cfg *cfg);

#endif // _AMP_CONTROLLER_H_
