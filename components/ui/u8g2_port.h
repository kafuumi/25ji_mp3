//
// Created by kafuumi on 2025/3/12.
//

#ifndef U8G2_PORT_H
#define U8G2_PORT_H

#include "driver/i2c_types.h"
#include "esp_err.h"
#include "u8g2/u8g2.h"

typedef enum {
    ROTATION_0 = 0,
    ROTATION_90 = 90,
    ROTATION_180 = 180,
    ROTATION_270 = 270,
} SCREEN_ROTATION; // screen rotation

typedef struct {
    i2c_master_bus_handle_t i2c_bus; // i2c bus with esp-idf i2c_master driver
    i2c_addr_bit_len_t dev_addr_length;
    size_t buf_size;
    uint16_t dev_address;
    uint32_t scl_freq_hz;
    int i2c_wait_time;
    SCREEN_ROTATION rotation;
} u8g2_port_i2c_config_t; // screen config for i2c

esp_err_t u8g2_port_init(const u8g2_port_i2c_config_t *cfg, u8g2_t *u8g2);

#endif //U8G2_PORT_H
