#if !defined(_AMP_ELEMENT_PRIV_H_)
#define _AMP_ELEMENT_PRIV_H_

#include <sys/queue.h>

#include "esp_event.h"

#include "amp/element.h"
#include "dashboard.h"

#define NOTIFY_VALUE_MASK_STATE (1 << 0)
#define NOTIFY_VALUE_MASK_EOS (1 << 1)
#define NOTIFY_VALUE_MASK_EOS_DONE (1 << 2)

struct amp_element {
    STAILQ_ENTRY(amp_element) stailq_entry;

    char *name;
    int stack_size;
    int affinity_core;
    int task_priority;
    enum amp_element_role role;
    const amp_element_interface_t *intf;

    TaskHandle_t task;
    TaskHandle_t controller_task;
    esp_event_handler_t event_bus;
    amp_dashboard_handle_t dashboard;
};

void amp_element_notify_event(amp_element_handle_t el, uint32_t notify_value);

#endif // _AMP_ELEMENT_PRIV_H_
