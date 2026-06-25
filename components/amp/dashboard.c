
#include "esp_log.h"

#include "amp/amp_mem.h"
#include "dashboard.h"

static const char *TAG = "dashboard";

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

void amp_dashboard_send_done(amp_dashboard_handle_t dashboard) {
    if (xSemaphoreGive(dashboard->done_count) != pdTRUE) {
        ESP_LOGW(TAG, "give done count sem fail");
    }
}
