#if !defined(_AMP_ELEMENT_H_)
#define _AMP_ELEMENT_H_

#include "esp_err.h"
#include "esp_event.h"

#include "amp/ringbuf.h"

#define AMP_ELEMENT_ENTRY() struct amp_element

typedef struct amp_element_interface {
    void (*run_task)(void *);
    void (*set_input_rb)(void *, ringbuf_handle_t);
    void (*set_output_rb)(void *, ringbuf_handle_t);
    esp_err_t (*register_events)(void *, esp_event_loop_handle_t);
    void (*deinit)(void *);
} amp_element_interface_t;

typedef struct {
    const char *name;
    const int stack_size;
    const size_t output_rb_size;
    const amp_element_interface_t *intf;
} amp_element_task_config_t;

enum amp_element_role {
    AMP_ELEMENT_READER,
    AMP_ELEMENT_PROCESSOR,
    AMP_ELEMENT_WRITER,
};

typedef struct amp_element *amp_element_handle_t;

#endif // _AMP_ELEMENT_H_
