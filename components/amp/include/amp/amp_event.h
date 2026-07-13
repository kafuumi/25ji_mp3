#if !defined(_AMP_EVENT_H_)
#define _AMP_EVENT_H_

#include "amp/audio_types.h"
#include "esp_event.h"

/**
 * @brief Internal event base used by elements to report status back to the controller
 */
ESP_EVENT_DECLARE_BASE(AMP_EVENT_REPORT);

#define AMP_EVENT_REPORT_ANY ESP_EVENT_ANY_ID
/**
 * @brief Event IDs reported by internal amp elements
 */
enum amp_event_report_id {
    AMP_EVENT_REPORT_FATAL,           /*!< Fatal error reported by an element */
    AMP_EVENT_REPORT_STREAM_METADATA, /*!< media type, file name */
    AMP_EVENT_REPORT_STREAM_DETAIL,   /*!< bit width, channels, etc */
};

typedef struct {
    /* metadata */
    struct {
        const char *audio_name;
        enum amp_audio_media_type media_type;
    };
    /* detail */
    struct {
        int sample_rate;
        int bit_width;
        int channel;
    };
} amp_event_msg_t;

#endif // _AMP_EVENT_H_
