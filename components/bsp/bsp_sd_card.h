#include <stdbool.h>
#if !defined(_BSP_SD_CARD_H_)
#define _BSP_SD_CARD_H_

#include "esp_err.h"

#define BSP_SD_CARD_MOUNT_POINT "/sdcard"

// 初始化
esp_err_t bsp_sd_card_init();
// 去挂载 sd card
esp_err_t bsp_sd_card_unmount();
// 格式化 sd card
esp_err_t bsp_sd_card_format();
// 测试 sd card 读写能力
bool bsp_sd_card_rw_test();

#endif // _BSP_SD_CARD_H_