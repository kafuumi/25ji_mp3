
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
