
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "amp/amp_mem.h"
#include "dashboard.h"

static const char *TAG = "dashboard";
struct amp_dashboard {
    _Atomic enum amp_state state;
    SemaphoreHandle_t done_count;
};

esp_err_t amp_dashboard_init(amp_dashboard_handle_t *dashboard) {
    amp_dashboard_handle_t dash = amp_calloc(1, sizeof(struct amp_dashboard));
    if (!dash) {
        return ESP_ERR_NO_MEM;
    }
    dash->state = AMP_STATE_READY;
    dash->done_count = NULL;
    *dashboard = dash;
    return ESP_OK;
}

void amp_dashboard_deinit(amp_dashboard_handle_t dashboard) {
    if (!dashboard) {
        return;
    }
    amp_free(dashboard);
}

enum amp_state amp_dashboard_swap_status(amp_dashboard_handle_t dashboard, enum amp_state new_state) {
    if (!dashboard) {
        return AMP_STATE_INVALID;
    }
    return atomic_exchange(&dashboard->state, new_state);
}

enum amp_state amp_dashboard_load_state(amp_dashboard_handle_t dashboard) {
    if (!dashboard) {
        return AMP_STATE_INVALID;
    }
    return atomic_load(&dashboard->state);
}

bool amp_dashboard_is_playing(amp_dashboard_handle_t dashboard) {
    if (!dashboard) {
        return false;
    }
    return atomic_load(&dashboard->state) == AMP_STATE_PLAYING;
}

BaseType_t amp_dashboard_set_done_count(amp_dashboard_handle_t dashboard, int size) {
    SemaphoreHandle_t sem = xSemaphoreCreateCounting(size, 0);
    if (!sem) {
        ESP_LOGE(TAG, "create semaphore fail");
        return pdFALSE;
    }

    dashboard->done_count = sem;
    return pdTRUE;
}

void amp_dashboard_send_done(amp_dashboard_handle_t dashboard) {}

BaseType_t amp_dashboard_take_done(amp_dashboard_handle_t dashboard, TickType_t timeout) { return 0; }