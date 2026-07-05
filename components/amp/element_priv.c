#include "esp_log.h"

#include "element_priv.h"

static const char *TAG = "element_priv";

void amp_element_notify_event(amp_element_handle_t el, uint32_t notify_value) {
    if (el->role == AMP_ELEMENT_WRITER) {
        ESP_LOGI(TAG, "element %s notify %d", el->name, notify_value);
        xTaskNotify(el->controller_task, notify_value, eSetBits);
    } else {
        ESP_LOGI(TAG, "element %s notify %d, ignored", el->name, notify_value);
    }
}
