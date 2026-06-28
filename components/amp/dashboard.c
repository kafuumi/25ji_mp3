
#include "esp_log.h"

#include "amp/amp_mem.h"
#include "dashboard.h"

static const char *TAG = "dashboard";

#define media_detail_lock(dash, timeout) xSemaphoreTake((dash)->media_sem, timeout)
#define media_detail_unlock(dash) xSemaphoreGive((dash)->media_sem)

esp_err_t amp_dashboard_init(amp_dashboard_handle_t *dashboard) {
    amp_dashboard_handle_t dash = amp_calloc(1, sizeof(struct amp_dashboard));
    if (!dash) {
        return ESP_ERR_NO_MEM;
    }
    atomic_init(&dash->state, AMP_STATE_READY);
    dash->media_sem = xSemaphoreCreateMutex();
    *dashboard = dash;
    return ESP_OK;
}

void amp_dashboard_deinit(amp_dashboard_handle_t dashboard) {
    if (!dashboard) {
        return;
    }
    amp_free(dashboard);
}

esp_err_t amp_dashboard_load_audio_detail(amp_dashboard_handle_t dashboard, struct amp_audio_detail *detail,
                                          TickType_t timeout) {
    if (!dashboard || !detail) {
        return ESP_ERR_INVALID_ARG;
    }
    if (media_detail_lock(dashboard, timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *detail = dashboard->media_detail;
    media_detail_unlock(dashboard);
    return ESP_OK;
}

esp_err_t amp_dashboard_swap_audio_detail(amp_dashboard_handle_t dashboard, struct amp_audio_detail *detail,
                                          TickType_t timeout) {
    if (!dashboard || !detail) {
        return ESP_ERR_INVALID_ARG;
    }
    if (media_detail_lock(dashboard, timeout) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    struct amp_audio_detail old = dashboard->media_detail;
    dashboard->media_detail = *detail;
    *detail = old;
    media_detail_unlock(dashboard);
    return ESP_OK;
}
