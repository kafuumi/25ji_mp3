
#include "bsp.h"
#include "driver/gpio.h"
#include "hal/gpio_types.h"

#define BSP_MUTE_LEVEL 0
#define BSP_UNMUTE_LEVEL 1

esp_err_t bsp_init() { return gpio_set_direction(BSP_PIN_I2S_MUTE, GPIO_MODE_OUTPUT); }

esp_err_t bsp_audio_mute(bool mute) {
    if (mute) {
        gpio_set_level(BSP_PIN_I2S_MUTE, BSP_MUTE_LEVEL);
    } else {
        gpio_set_level(BSP_PIN_I2S_MUTE, BSP_UNMUTE_LEVEL);
    }
    return ESP_OK;
}
