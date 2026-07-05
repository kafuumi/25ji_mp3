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
    AMP_EVENT_REPORT_FATAL,        /*!< Fatal error reported by an element */
    AMP_EVENT_REPORT_STREAM_EOS,   /*!< End of stream reported by reader */
    AMP_EVENT_REPORT_AUDIO_FORMAT, /*!< Audio format change (media type, sample rate, etc.) */
    AMP_EVENT_REPORT_AUDIO_DETAIL, /*!< Audio detail update (bit width, channels, etc.) */
};

#endif // _AMP_EVENT_H_
