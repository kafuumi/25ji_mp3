#if !defined(_BSP_H_)
#define _BSP_H_

#include <stdbool.h>

#include "bsp_sd_card.h"
#include "esp_err.h"

/*
 * I2C PIN DEFINITION
 */
#define BSP_PIN_I2C_SCL GPIO_NUM_39
#define BSP_PIN_I2C_SDA GPIO_NUM_38

/*
 * I2S PING DEFINTION
 */
// PCM 5102A 软静音引脚（未设置上拉，需要推挽输出）
#define BSP_PIN_I2S_MUTE GPIO_NUM_15
#define BSP_PIN_I2S_WS GPIO_NUM_16
#define BSP_PIN_I2S_DOUT GPIO_NUM_17
#define BSP_PIN_I2S_BCK GPIO_NUM_18
#define BSP_PIN_I2S_MCK GPIO_NUM_8

/*
 * button defintion
 */
#define BSP_PIN_BTN_PREV GPIO_NUM_5
#define BSP_PIN_BTN_NEXT GPIO_NUM_6
#define BSP_PIN_BTN_ANY GPIO_NUM_21
/* ADC_CHANNEL_1 */
#define BSP_PIN_BTN_PLUSTOR GPIO_NUM_2
/* GPIO_NUM_2 */
#define BSP_PIN_BTN_PLUSTOR_ADC_CHAN ADC_CHANNEL_1

#define BSP_PIN_BTN_ACTIVE_LEVEL 1
#define BSP_PIN_BTN_DEACTIVE_LEVEL 0

typedef void (*volume_change_handler)(int volume, int diff);

esp_err_t bsp_init();

esp_err_t bsp_audio_mute(bool mute);

esp_err_t bsp_btn_plustor_registor_cb(volume_change_handler cb, int min_val, int internal);

#endif // _BSP_H_
