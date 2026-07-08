#include "esp_log.h"

#include "element_priv.h"

static const char *TAG = "element_priv";

void amp_element_notify_event(amp_element_handle_t el, uint32_t notify_value) {
    switch (notify_value) {
    case NOTIFY_VALUE_MASK_STREAM_END:
    case NOTIFY_VALUE_MASK_STREAM_ABORT:
        if (el->role == AMP_ELEMENT_WRITER) {
            ESP_LOGI(TAG, "element %s notify %d", el->name, notify_value);
            xTaskNotify(el->controller_task, notify_value, eSetBits);
        } else {
            ESP_LOGI(TAG, "element %s notify %d, ignored", el->name, notify_value);
        }
    }
}

void amp_element_task_done(amp_element_handle_t el) {
    if (el && el->done_sem) {
        ESP_LOGI(TAG, "notify element %s task done", el->name);
        xSemaphoreGive(el->done_sem);
    }
}
