#include "aht20.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "bsp.h"
#include "bsp_sd_card.h"
#include "ui.h"

#define PORT_I2C I2C_NUM_0

static const char *TAG = "app";

static i2c_master_bus_handle_t i2c_bus_handle = NULL;

static esp_err_t i2c_bus_init() {
    if (i2c_bus_handle != NULL) {
        ESP_LOGW(TAG, "i2c bus was initialized");
        return ESP_OK;
    }
    const i2c_master_bus_config_t i2c_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = false,
        .i2c_port = PORT_I2C,
        .scl_io_num = BSP_PIN_I2C_SCL,
        .sda_io_num = BSP_PIN_I2C_SDA,
    };
    esp_err_t err = i2c_new_master_bus(&i2c_cfg, &i2c_bus_handle);
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "fnew i2c master bus fail: %d(%s)", err, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "success to initialize i2c bus, sda: %d, scl: %d", i2c_cfg.sda_io_num, i2c_cfg.scl_io_num);
    return ESP_OK;
}

static void sensor_read_task(void *args) {
    aht20_init(i2c_bus_handle);
    // audio_test();
    while (true) {
        float temp, humi;
        esp_err_t err = aht20_read_temperature_humidity(&temp, &humi);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "read sensor fail: %s", esp_err_to_name(err));
            continue;
        }
        ESP_LOGI(TAG, "read sensor: %.2f, %.2f", temp, humi);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void app_main(void) {
    printf("esp32s3 startup, enter app_main()\n");
    esp_log_level_set("U8G2_PORT", ESP_LOG_WARN);
    esp_err_t err;

    bsp_init();
    err = bsp_sd_card_mount();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp init sd card fail: %s", esp_err_to_name(err));
        return;
    }
    bsp_audio_mute(false);
    err = i2c_bus_init();
    if (ESP_OK != err) {
        ESP_LOGE(TAG, "new i2c master bus fail: %s", esp_err_to_name(err));
        return;
    }

    xTaskCreate(sensor_read_task, "sensor", 4096, NULL, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(1000));

    // err = ui_init(i2c_bus_handle);
    // if (ESP_OK != err) {
    //     ESP_LOGE(TAG, "init ui fail: %s", esp_err_to_name(err));
    //     return;
    // }

    // const u8g2_port_i2c_config_t u8g2_port_cfg = {
    //     .i2c_bus = i2c_bus_handle,
    //     .rotation = ROTATION_180,
    //     .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    //     .dev_address = 0x3C,
    //     .scl_freq_hz = 100 * 1000,
    //     .buf_size = 32,
    // };
    // u8g2 = (u8g2_t *) malloc(sizeof(u8g2_t));
    // err = u8g2_i2c_init(&u8g2_port_cfg, u8g2);
    // ESP_ERROR_CHECK(err);
    // u8g2_SetFont(u8g2, u8g2_font_ncenB08_tr);
}
