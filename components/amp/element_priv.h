#if !defined(_AMP_ELEMENT_PRIV_H_)
#define _AMP_ELEMENT_PRIV_H_

#include <sys/queue.h>

#include "esp_event.h"

#include "amp/element.h"
#include "dashboard.h"

#define NOTIFY_VALUE_MASK_STATE 1 << 0
#define NOTIFY_VALUE_MASK_MEDIA_TYPE 1 << 1
#define NOTIFY_VALUE_MASK_MEDIA_DETAIL 1 << 2
#define NOTIFY_VALUE_MASK_EOS 1 << 3
#define NOTIFY_VALUE_MASK_EOS_DONE 1 << 4

struct amp_element {
    STAILQ_ENTRY(amp_element) stailq_entry;

    char *name;
    int stack_size;
    enum amp_element_role role;
    const amp_element_interface_t *intf;

    TaskHandle_t task;
    esp_event_handler_t event_bus;
    amp_dashboard_handle_t dashboard;
};

#define AMP_EL_SEND_DONE(TAG, el, field)                                                                               \
    do {                                                                                                               \
        if (xSemaphoreGive((el)->field.dashboard->done_count) != pdTRUE) {                                             \
            ESP_LOGE(TAG, "give done count sema fail");                                                                \
        }                                                                                                              \
    } while (0)

#endif // _AMP_ELEMENT_PRIV_H_
