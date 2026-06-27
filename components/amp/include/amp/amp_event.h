#if !defined(_AMP_EVENT_H_)
#define _AMP_EVENT_H_

#include "esp_event.h"
/**
 * @brief Internal event base used by elements to report status back to the controller
 */
ESP_EVENT_DECLARE_BASE(AMP_EVENT_REPORT);

/**
 * @brief Event IDs reported by internal amp elements
 */
enum amp_event_report_id {
    AMP_EVENT_REPORT_FATAL, /*!< Fatal error reported by an element */
    AMP_EVENT_REPORT_EOS,
    AMP_EVENT_REPORT_MEDIA_TYPE,
    AMP_EVENT_REPORT_MEDIA_DETAIL,
};

// 外部操作事件
ESP_EVENT_DECLARE_BASE(AMP_EVENT_ACTION);

enum amp_event_action_id {
    AMP_EVENT_ACTION_PLAY = 1,   // Ready => Playing
    AMP_EVENT_ACTION_PAUSE = 2,  // Playing => Paused
    AMP_EVENT_ACTION_RESUME = 3, // Paused => Playing
    AMP_EVENT_ACTION_RESET = 4,  // any => Ready
};

#endif // _AMP_EVENT_H_
