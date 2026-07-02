
#include "driver/gpio.h"
#include "esp_check.h"
#include "hal/gpio_types.h"

#include "bsp.h"

#define BSP_MUTE_LEVEL 0
#define BSP_UNMUTE_LEVEL 1

static const char *TAG = "bsp";

esp_err_t bsp_init() {
    esp_err_t err;

    err = gpio_set_direction(BSP_PIN_I2S_MUTE, GPIO_MODE_OUTPUT);
    ESP_RETURN_ON_ERROR(err, TAG, "set mute gpio ping direction fail: %d(%s)", err, esp_err_to_name(err));

    err = bsp_sd_card_mount();
    return err;
}

esp_err_t bsp_audio_mute(bool mute) {
    if (mute) {
        gpio_set_level(BSP_PIN_I2S_MUTE, BSP_MUTE_LEVEL);
    } else {
        gpio_set_level(BSP_PIN_I2S_MUTE, BSP_UNMUTE_LEVEL);
    }
    return ESP_OK;
}
