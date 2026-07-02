#if !defined(_BSP_SD_CARD_H_)
#define _BSP_SD_CARD_H_

#include "esp_err.h"

#define BSP_SD_CARD_MOUNT_POINT "/sdcard"

/*
 * SD CARD PIN
 */
#define BSP_PIN_SD_CLK GPIO_NUM_12
#define BSP_PIN_SD_CMD GPIO_NUM_11
#define BSP_PIN_SD_D0 GPIO_NUM_13
#define BSP_PIN_SD_D1 GPIO_NUM_14
#define BSP_PIN_SD_D2 GPIO_NUM_9
#define BSP_PIN_SD_D3 GPIO_NUM_10

esp_err_t bsp_sd_card_mount();

esp_err_t bsp_sd_card_unmount();

#endif // _BSP_SD_CARD_H_
