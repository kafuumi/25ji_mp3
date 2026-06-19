#if !defined(_AMP_CONTROLLER_H_)
#define _AMP_CONTROLLER_H_

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/ringbuf.h"
#include <sys/queue.h>

#include "amp/element.h"

typedef struct amp_controller amp_controller_handle_t;

// 外部操作事件
ESP_EVENT_DECLARE_BASE(AMP_EVENT_ACTION);

enum amp_event_action_id {
    AMP_EVENT_ACTION_PAUSE, // pause
};

// 内部组件上报事件
ESP_EVENT_DECLARE_BASE(AMP_EVENT_REPORT);

enum amp_event_report_id {
    AMP_EVENT_REPORT_FATAL,
};

// audio output arguments changed
struct amp_event_report_audio_args {
    int sample_rate;
};

esp_err_t amp_controller_run(amp_controller_handle_t *controller);

esp_err_t amp_controller_init(amp_controller_handle_t **controller);

esp_err_t amp_controller_append_reader(amp_controller_handle_t *controller, amp_element_handle_t *el,
                                       const amp_element_interface_t *intf, const struct amp_element_task_cfg *cfg);

esp_err_t amp_controller_append_writer(amp_controller_handle_t *controller, amp_element_handle_t *el,
                                       const amp_element_interface_t *intf, const struct amp_element_task_cfg *cfg);

esp_err_t amp_controller_pause(amp_controller_handle_t *controller);

#endif // _AMP_CONTROLLER_H_
