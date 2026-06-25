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
    AMP_STATE_INVALID,      /*!< Invalid / unknown state */
    AMP_STATE_READY,        /*!< Initialized, not playing */
    AMP_STATE_PLAYING,      /* playing music */
    AMP_STATE_WAITING_NEXT, /* a music source strem is end, waiting all component process finished */
    AMP_STATE_PAUSE,        /* pause state */
    AMP_STATE_FATAL,        /* any error acour */
};

struct amp_audio_info {
    const char *name;
    int sample_rate;
    enum amp_audio_media_type media_type;
    enum amp_audio_channel channel;
    enum amp_audio_bit_width bit_width;
};

struct amp_dashboard {
    _Atomic enum amp_state state;
    SemaphoreHandle_t done_count;
    volatile struct amp_audio_info audio;
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
enum amp_state amp_dashboard_swap_status(amp_dashboard_handle_t dashboard, enum amp_state new_state);

/**
 * @brief Atomically load the current state
 *
 * @param dashboard  Dashboard handle
 * @return Current stored state
 */
enum amp_state amp_dashboard_load_state(amp_dashboard_handle_t dashboard);

/**
 * @brief Atomically check current state is AMP_STATE_PLAYING
 *
 * @param dashboard  Dashboard handle
 * @return true when current state is AMP_STATE_PLAYING
 */
bool amp_dashboard_is_playing(amp_dashboard_handle_t dashboard);

void amp_dashboard_send_done(amp_dashboard_handle_t dashboard);

#ifdef __cplusplus
}
#endif

#endif // _AMP_DASHBOARD_H_
