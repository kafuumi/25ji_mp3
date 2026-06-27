#if !defined(_AMP_DASHBOARD_H_)
#define _AMP_DASHBOARD_H_

#include <stdatomic.h>
#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "amp/audio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Player state
 */
enum amp_state {
    AMP_STATE_INVALID, /*!< Invalid / unknown state */
    AMP_STATE_READY,   /*!< Initialized, not playing */
    AMP_STATE_PLAYING, /* playing music */
    AMP_STATE_PAUSE,   /* pause state */
    AMP_STATE_FATAL,   /* any error acour */
};

struct amp_dashboard {
    _Atomic enum amp_state state;
    SemaphoreHandle_t done_count;
    /* media info */
    struct {
        volatile const char *name;
        _Atomic int sample_rate;
        _Atomic enum amp_audio_media_type media_type;
        _Atomic enum amp_audio_channel channel;
        _Atomic enum amp_audio_bit_width bit_width;
    };
};

/**
 * @brief representing a single dashboard handle
 */
typedef struct amp_dashboard *amp_dashboard_handle_t;

/**
 * @brief Allocate and initialize a dashboard
 *
 * @param[out] dashboard  Pointer to the newly created dashboard handle
 * @return
 *     - ESP_OK              Dashboard created successfully
 *     - ESP_ERR_NO_MEM      Insufficient memory
 */
esp_err_t amp_dashboard_init(amp_dashboard_handle_t *dashboard);

/**
 * @brief Deinitialize and free a dashboard
 *
 * @param dashboard  Dashboard handle created by amp_dashboard_init
 */
void amp_dashboard_deinit(amp_dashboard_handle_t dashboard);

/**
 * @brief Atomically swap the current state with a new state
 *
 * @param dashboard  Dashboard handle
 * @param new_state  New state to set
 * @return Previous state before the swap
 */
#define AMP_DASH_SWAP_STATE(dash, new_state) atomic_exchange(&(dash)->state, new_state)

/**
 * @brief Atomically load the current state
 *
 * @param dashboard  Dashboard handle
 * @return Current stored state
 */
#define AMP_DASH_LOAD_STATE(dash) atomic_load(&(dash)->state)

/**
 * @brief Atomically check current state is AMP_STATE_PLAYING
 *
 * @param dashboard  Dashboard handle
 * @return true when current state is AMP_STATE_PLAYING
 */
#define AMP_DASH_IS_PLAYING(dash) AMP_DASH_LOAD_STATE(dash) == AMP_STATE_PLAYING

#define AMP_DASH_LOAD_MEDIA_TYPE(dash) atomic_load(&(dash)->media_type)

#define AMP_DASH_SET_MEDIA_TYPE(dash, type) atomic_store(&(dash)->media_type, type)

#ifdef __cplusplus
}
#endif

#endif // _AMP_DASHBOARD_H_
