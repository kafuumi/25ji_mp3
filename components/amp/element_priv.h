#if !defined(_AMP_ELEMENT_PRIV_H_)
#define _AMP_ELEMENT_PRIV_H_

#include <sys/queue.h>

#include "esp_event.h"

#include "amp/element.h"
#include "dashboard.h"

// 外部操作事件
ESP_EVENT_DECLARE_BASE(AMP_EVENT_ACTION);

enum amp_event_action_id {
    AMP_EVENT_ACTION_PLAY = 1,   // Ready => Playing
    AMP_EVENT_ACTION_PAUSE = 2,  // Playing => Paused
    AMP_EVENT_ACTION_RESUME = 3, // Paused => Playing
    AMP_EVENT_ACTION_RESET = 4,  // any => Ready
};

// audio output arguments changed
struct amp_event_report_audio_args {
    int sample_rate;
};

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

#endif // _AMP_ELEMENT_PRIV_H_
