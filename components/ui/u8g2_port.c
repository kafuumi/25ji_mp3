//
// Created by kafuumi on 2025/3/12.
//

#include "freertos/FreeRTOS.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_check.h"

#include "u8g2_port.h"

#define WAIT_TIME 100 // 100 ms
#define BUF_SIZE 32

static const char *TAG = "U8G2_PORT";
static i2c_master_dev_handle_t i2c_dev_handle = NULL;
static int i2c_timeout = WAIT_TIME;

static uint8_t gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

static uint8_t u8x8_byte_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

static uint8_t gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch (msg) {
        case U8X8_MSG_GPIO_RESET:
            // reset
            ESP_LOGI(TAG, "gpio: reset, arg_ind=%d", arg_int);
            break;
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            // init
            ESP_LOGI(TAG, "gpio: gpio and delay init, arg_int=%d", arg_int);
            break;
        case U8X8_MSG_DELAY_MILLI:
            ESP_LOGI(TAG, "gpio: delay %d ms", arg_int);
            vTaskDelay(pdMS_TO_TICKS(arg_int));
            break;
        default:
            // ignore
            break;
    }
    return true;
}

static uint8_t *i2c_buf = NULL;

static uint8_t u8x8_byte_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    static size_t count = 0;

    if (i2c_dev_handle == NULL) {
        ESP_LOGW(TAG, "i2c: device is not initialized");
        return false;
    }
    esp_err_t err = ESP_OK;
    switch (msg) {
        case U8X8_MSG_BYTE_INIT:
            /* add your custom code to init i2c subsystem */
            ESP_LOGI(TAG, "i2c: initialize, arg_int=%d", arg_int);
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            ESP_LOGI(TAG, "i2c: transfer start");
        // reset buffer size
            count = 0;
            break;
        case U8X8_MSG_BYTE_SEND:
            ESP_LOGI(TAG, "i2c: transfer bytes, size=%d", arg_int);
            uint8_t *data_ptr = (uint8_t *) arg_ptr;
            ESP_LOG_BUFFER_HEXDUMP(TAG, data_ptr, arg_int, ESP_LOG_DEBUG);

        // write data to buffer
            for (int i = 0; i < arg_int; i++) {
                i2c_buf[count] = *data_ptr;
                data_ptr++;
                count++;
            }
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            ESP_LOGI(TAG, "i2c: transfer end, total bytes=%d", count);
            if (0 == count) {
                ESP_LOGI(TAG, "i2c: total bytes is 0, do not send");
            } else {
                err = i2c_master_transmit(i2c_dev_handle, i2c_buf, count, i2c_timeout);
            }
            break;
        default:
            ESP_LOGI(TAG, "i2c: msg=%d, arg_int=%d, arg_ptr=0x%p", msg, arg_int, arg_ptr);
            break;
    }
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "i2c: failed to transfer bytes, err:%d(%s)", err, esp_err_to_name(err));
        return false;
    }
    return true;
}

esp_err_t u8g2_port_init(const u8g2_port_i2c_config_t *cfg, u8g2_t *u8g2) {
    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "u8g2 port i2c config is NULL");
    ESP_RETURN_ON_FALSE(u8g2, ESP_ERR_INVALID_ARG, TAG, "u8g2 context is NULL");
    ESP_RETURN_ON_FALSE(cfg->i2c_bus, ESP_ERR_INVALID_ARG, TAG, "i2c bus handle is NULL");

    const i2c_device_config_t dev_cfg = {
        .device_address = cfg->dev_address,
        .scl_speed_hz = cfg->scl_freq_hz,
        .dev_addr_length = cfg->dev_addr_length,
        .flags.disable_ack_check = false,
    };
    esp_err_t err = i2c_master_bus_add_device(cfg->i2c_bus, &dev_cfg, &i2c_dev_handle);
    ESP_RETURN_ON_ERROR(err, TAG, "failed to add device to i2c bus, err:%d(%s)", err, esp_err_to_name(err));

    if (cfg->i2c_wait_time > 0 && cfg->i2c_wait_time < 500) {
        i2c_timeout = cfg->i2c_wait_time;
    }
    if (cfg->buf_size > 0) {
        i2c_buf = (uint8_t *) malloc(cfg->buf_size);
    } else {
        i2c_buf = (uint8_t *) malloc(BUF_SIZE);
    }

    ESP_LOGI(TAG, "start setup u8g2 hardware, set rotation to %d", cfg->rotation);
    switch (cfg->rotation) {
        case ROTATION_90:
            u8g2_Setup_sh1106_i2c_128x64_noname_f(u8g2, U8G2_R1,
                                                  u8x8_byte_hw_i2c, gpio_and_delay_cb);
            break;
        case ROTATION_180:
            u8g2_Setup_sh1106_i2c_128x64_noname_f(u8g2, U8G2_R2,
                                                  u8x8_byte_hw_i2c, gpio_and_delay_cb);
            break;
        case ROTATION_270:
            u8g2_Setup_sh1106_i2c_128x64_noname_f(u8g2, U8G2_R3,
                                                  u8x8_byte_hw_i2c, gpio_and_delay_cb);
            break;
        case ROTATION_0:
        default:
            u8g2_Setup_sh1106_i2c_128x64_noname_f(u8g2, U8G2_R0,
                                                  u8x8_byte_hw_i2c, gpio_and_delay_cb);
            break;
    }

    u8g2_InitDisplay(u8g2);
    u8g2_SetPowerSave(u8g2, false);
    // clear screen
    u8g2_ClearBuffer(u8g2);
    u8g2_SendBuffer(u8g2);

    ESP_LOGI(TAG, "initialize u8g2 successful");
    return ESP_OK;
}
