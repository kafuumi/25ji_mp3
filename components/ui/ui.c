#include "driver/i2c_types.h"
#include "u8g2/u8g2.h"
#include "esp_log.h"
#include "esp_check.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "driver/gpio.h"
#include "bsp.h"
#include "ui.h"
#include "u8g2_port.h"

#define LOW 0
#define HIGH 1

static const char *TAG = "ui";
static u8g2_t *u8g2_ctx = NULL;

// button
static button_handle_t prev_btn = NULL;
static button_handle_t next_btn = NULL;
static button_handle_t any_btn = NULL;

void prev_btn_click_evt(void *btn, void *data){
    ESP_LOGI(TAG, "prev btn click");
}

void next_btn_click_evt(void *btn, void *data){
    ESP_LOGI(TAG, "next btn click");
}

esp_err_t ui_btn_init() {
    const button_config_t btn_cfg = {0};
    button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = GPIO_NUM_5,
        .disable_pull = true,
        .active_level = HIGH,
    };
    esp_err_t err = ESP_OK;
    err = iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &prev_btn);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create prev button");
        return err;
    }
    button_event_args_t btn_evt_args = {
        .multiple_clicks.clicks = 2,
    };
    iot_button_register_cb(prev_btn, BUTTON_SINGLE_CLICK, &btn_evt_args, prev_btn_click_evt, NULL);
    iot_button_register_cb(prev_btn, BUTTON_MULTIPLE_CLICK, &btn_evt_args, prev_btn_click_evt, NULL);
    btn_gpio_cfg.gpio_num = GPIO_NUM_5;
    return ESP_OK;
}

esp_err_t ui_init(i2c_master_bus_handle_t i2c_bus_handle) {
   const u8g2_port_i2c_config_t u8g2_port_cfg = {
      .i2c_bus = i2c_bus_handle,
      .rotation = ROTATION_180,
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .dev_address = 0x3C,
      .scl_freq_hz = 100 * 1000,
      .buf_size = 32,
  };
  u8g2_t *u8g2 = (u8g2_t *) malloc(sizeof(u8g2_t));
  esp_err_t err = u8g2_port_init(&u8g2_port_cfg, u8g2);
  if (ESP_OK != err) {
    return err;
  }
  u8g2_ctx = u8g2;
  u8g2_SetFont(u8g2, u8g2_font_ncenB08_tr);
  u8g2_ClearDisplay(u8g2);
  return ui_btn_init();
}
