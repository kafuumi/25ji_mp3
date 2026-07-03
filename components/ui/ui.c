#include "driver/i2c_types.h"
#include "u8g2/u8g2.h"
#include "esp_log.h"
#include "esp_check.h"

#include "bsp.h"
#include "ui.h"
#include "u8g2_port.h"

#define LOW 0
#define HIGH 1

static const char *TAG = "ui";
static u8g2_t *u8g2_ctx = NULL;

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
  return ESP_OK;
}
