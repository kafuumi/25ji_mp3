#if !defined(_AMP_CONTROLLER_H_)
#define _AMP_CONTROLLER_H_

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/ringbuf.h"
#include <sys/queue.h>

#include "amp/element.h"

typedef struct amp_controller amp_controller_handle_t;

// 内部组件上报事件
ESP_EVENT_DECLARE_BASE(AMP_EVENT_REPORT);

enum amp_event_report_id {
    AMP_EVENT_REPORT_FATAL,
};

esp_err_t amp_controller_run(amp_controller_handle_t *controller);

esp_err_t amp_controller_init(amp_controller_handle_t **controller);

esp_err_t amp_controller_append_reader(amp_controller_handle_t *controller, amp_element_handle_t *el,
                                       const amp_element_interface_t *intf, const struct amp_element_task_cfg *cfg);

esp_err_t amp_controller_append_writer(amp_controller_handle_t *controller, amp_element_handle_t *el,
                                       const amp_element_interface_t *intf, const struct amp_element_task_cfg *cfg);

esp_err_t amp_controller_action_play(amp_controller_handle_t *controller);

esp_err_t amp_controller_action_pause(amp_controller_handle_t *controller);

esp_err_t amp_controller_action_reset(amp_controller_handle_t *controller);

esp_err_t amp_controller_action_toggle_play(amp_controller_handle_t *controller, bool *to_play);

#endif // _AMP_CONTROLLER_H_
