#if !defined(_AMP_ELEMENT_PRIV_H_)
#define _AMP_ELEMENT_PRIV_H_

#include <sys/queue.h>

#include "esp_event.h"

#include "amp/dashboard.h"
#include "amp/element.h"

struct amp_element {
    char *name;
    int stack_size;
    TaskHandle_t task;
    esp_event_handler_t event_bus;
    amp_dashboard_handle_t *dashboard;
    const amp_element_interface_t *intf;

    enum amp_element_role role;

    STAILQ_ENTRY(amp_element) stailq_entry;
};

#endif // _AMP_ELEMENT_PRIV_H_
