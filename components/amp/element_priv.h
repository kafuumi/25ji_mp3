#if !defined(_AMP_ELEMENT_PRIV_H_)
#define _AMP_ELEMENT_PRIV_H_

#include <sys/queue.h>

#include "esp_event.h"

#include "amp/element.h"
#include "dashboard.h"

#define NOTIFY_VALUE_MASK_STATE (1 << 0)
#define NOTIFY_VALUE_MASK_STREAM_NEW (1 << 1)
#define NOTIFY_VALUE_MASK_STREAM_END (1 << 2)
#define NOTIFY_VALUE_MASK_STREAM_ABORT (1 << 3)

#define NOTIFY_VALUE_MASK_STOP 0xFFFFFF

#define EL_WAIT_NOTIFY(notify, wait_time) if (xTaskNotifyWait(0, ULONG_MAX, &(notify), wait_time) == pdTRUE)
#define EL_NOTIFY_ON_WHAT(notify, mask) if ((notify & mask))
#define EL_NOTIFY_ON_STATE(notify) EL_NOTIFY_ON_WHAT(notify, NOTIFY_VALUE_MASK_STATE)
#define EL_NOTIFY_ON_STREAM_NEW(notify) EL_NOTIFY_ON_WHAT(notify, NOTIFY_VALUE_MASK_STREAM_NEW)
#define EL_NOTIFY_ON_STREAM_ABORT(notify) EL_NOTIFY_ON_WHAT(notify, NOTIFY_VALUE_MASK_STREAM_ABORT)
#define EL_NOTIFY_ON_STOP(notify) if (notify == NOTIFY_VALUE_MASK_STOP)

struct amp_element {
    STAILQ_ENTRY(amp_element) stailq_entry;

    char *name; /* free by controller */
    int stack_size;
    int affinity_core;
    int task_priority;
    enum amp_element_role role;
    const amp_element_interface_t *intf;

    TaskHandle_t task;
    TaskHandle_t controller_task;
    esp_event_handler_t event_bus;    /* free by controller, single instance */
    amp_dashboard_handle_t dashboard; /* free by controller, single instance */
    SemaphoreHandle_t done_sem;       /* free by controller */
};

void amp_element_notify_event(amp_element_handle_t el, uint32_t notify_value);

void amp_element_task_done(amp_element_handle_t el);

#endif // _AMP_ELEMENT_PRIV_H_
