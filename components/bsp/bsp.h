#if !defined(_BSP_H_)
#define _BSP_H_

#include "esp_err.h"
#include <stdbool.h>

/*
 * I2C PIN DEFINITION
 */
#define BSP_PIN_I2C_SCL GPIO_NUM_39
#define BSP_PIN_I2C_SDA GPIO_NUM_38

/*
 * SD CARD PIN
 */
#define BSP_PIN_SD_CLK GPIO_NUM_12
#define BSP_PIN_SD_CMD GPIO_NUM_11
#define BSP_PIN_SD_D0 GPIO_NUM_13
#define BSP_PIN_SD_D1 GPIO_NUM_14
#define BSP_PIN_SD_D2 GPIO_NUM_9
#define BSP_PIN_SD_D3 GPIO_NUM_10

/*
 * I2C PING DEFINTION
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

esp_err_t bsp_init();

esp_err_t bsp_audio_mute(bool mute);

#endif // _BSP_H_
