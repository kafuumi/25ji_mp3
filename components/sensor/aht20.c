//
// Created by kafuumi on 2025/3/6.
//

#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <inttypes.h>
#include <string.h>

#include "aht20.h"

#define WAIT_TIME (500 / portTICK_PERIOD_MS) // i2c 读写操作超时时间 500 ms

// aht20 基本命令

#define AHT20_CMD_INIT 0xBE  // 初始化
#define AHT20_CMD_AC 0xAC    // 测量
#define AHT20_CMD_RESET 0xBA // 软复位

#define AHT20_STATUS_CAL_ENABLE BIT(3) // 0b00001000
#define AHT20_STATUS_BUSY_FLAG BIT(7)  // 0b10000000

#define AHT20_SCL (100 * 1000) // 100 khz

#define INIT_CHECK()                                                           \
  ESP_RETURN_ON_FALSE(aht20_dev_handle, ESP_FAIL, TAG,                         \
                      "aht20 is not initialized!")

static i2c_master_dev_handle_t aht20_dev_handle = NULL;
static const char *TAG = "aht20";

esp_err_t aht20_status(uint8_t *status) {
  INIT_CHECK();
  esp_err_t err = i2c_master_receive(aht20_dev_handle, status, 1, WAIT_TIME);
  if (ESP_OK != err) {
    ESP_LOGE(TAG, "read aht20 status fail: %d(%s)", err, esp_err_to_name(err));
    return err;
  }
  ESP_LOGD(TAG, "get aht20 status success, raw: %d", *status);
  return ESP_OK;
}

void aht20_print_status(uint8_t status) {
  char *mode;
  uint8_t mode_val = (status & 0x60) >> 5;
  if (mode_val == 0) {
    mode = "NOR";
  } else if (mode_val == 1) {
    mode = "CYC";
  } else {
    mode = "CMD";
  }
  printf("aht20 status: busy: %d | mode: %s | enable: %d\n",
         (status & 0x80) >> 7, mode, (status & 0x08) >> 3);
}

esp_err_t aht20_reset() {
  INIT_CHECK();
  const uint8_t wb[1] = {AHT20_CMD_RESET};
  esp_err_t err = i2c_master_transmit(aht20_dev_handle, wb, 1, WAIT_TIME);
  if (ESP_OK != err) {
    ESP_LOGE(TAG, "send reset cmd fail: %d(%s)", err, esp_err_to_name(err));
    return err;
  }
  ESP_LOGI(TAG, "send reset cmd to aht20 success");
  return ESP_OK;
}

esp_err_t aht20_read_temperature_humidity(float *temperature, float *humidity) {
  INIT_CHECK();
  const uint8_t wb[3] = {AHT20_CMD_AC, 0x33, 0x00};
  uint8_t rb[6];
  memset(rb, 0, 6);

  esp_err_t err = i2c_master_transmit(aht20_dev_handle, wb, 3, WAIT_TIME);
  if (ESP_OK != err) {
    ESP_LOGE(TAG, "send AC cmd to aht20 fail: %d(%s)", err,
             esp_err_to_name(err));
    return err;
  }
  ESP_LOGD(TAG, "send AC cmd success, wait result");
  vTaskDelay(pdMS_TO_TICKS(100)); // 至少需要等待 75 ms 测量完成

  err = i2c_master_receive(aht20_dev_handle, rb, 6, WAIT_TIME);
  if (ESP_OK != err) {
    ESP_LOGE(TAG, "read AC result fail: %d(%s)", err, esp_err_to_name(err));
    return err;
  }
  uint8_t status = rb[0]; // 状态信息
  ESP_LOGD(TAG, "read data success, raw: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x", rb[0],
           rb[1], rb[2], rb[3], rb[4], rb[5]);
  bool cal_enable = status & AHT20_STATUS_CAL_ENABLE;
  bool is_busy = status & AHT20_STATUS_BUSY_FLAG;
  if (!cal_enable) {
    ESP_LOGE(TAG, "measure fail, cal enable is false");
    return AHT20_ERR_CAL_DISABLE;
  }
  if (is_busy) {
    ESP_LOGE(TAG, "measure fail, device is busy");
    return AHT20_ERR_BUSY;
  }
  uint32_t raw_humidity = rb[1];
  raw_humidity = (raw_humidity << 8) | rb[2];
  raw_humidity = (raw_humidity << 8) | rb[3];
  raw_humidity >>= 4;
  if (humidity)
    *humidity = (float)raw_humidity * 100 / 1048576; // raw/2^20 * 100%

  uint32_t raw_temperature = 0x0f & rb[3];
  raw_temperature = (raw_temperature << 8) | rb[4];
  raw_temperature = (raw_temperature << 8) | rb[5];
  if (temperature)
    *temperature =
        (float)raw_temperature * 200 / 1048576 - 50; // raw/2^20 * 200 - 50
  ESP_LOGD(TAG,
           "measure success, temperature: %" PRIu32 " => (%.2f), "
           "raw_humidity: %" PRIu32 " => (%.2f%%)",
           raw_temperature, *temperature, raw_humidity, *humidity);
  return ESP_OK;
}

esp_err_t aht20_dev_init() {
  uint8_t wb[3] = {AHT20_CMD_INIT, 0x08, 0x00};
  esp_err_t err = i2c_master_transmit(aht20_dev_handle, wb, 3, WAIT_TIME);
  if (ESP_OK != err) {
    ESP_LOGE(TAG, "send init cmd fail: %d(%s)", err, esp_err_to_name(err));
    return err;
  }
  ESP_LOGI(TAG,
           "success to initialize aht20 device, scl speed: %d, dev addr: %x",
           AHT20_SCL, AHT20_ADDR);
  return ESP_OK;
}

esp_err_t aht20_read_temperature(float *temperature) {
  return aht20_read_temperature_humidity(temperature, NULL);
}

esp_err_t aht20_read_humidity(float *humidity) {
  return aht20_read_temperature_humidity(NULL, humidity );
}

esp_err_t aht20_init(i2c_master_bus_handle_t bus_handle) {
  if (aht20_dev_handle != NULL) {
    ESP_LOGW(TAG, "aht20 device handle is not null, ignore init");
    return ESP_OK;
  }
  ESP_RETURN_ON_FALSE(bus_handle, ESP_ERR_INVALID_ARG, TAG,
                      "i2c master bus handle is null");

  ESP_LOGD(TAG, "start initialize aht20 device");

  i2c_device_config_t dev_cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = AHT20_ADDR,
      .scl_speed_hz = AHT20_SCL,
  };
  esp_err_t err =
      i2c_master_bus_add_device(bus_handle, &dev_cfg, &aht20_dev_handle);
  if (ESP_OK != err) {
    ESP_LOGE(TAG, "failed to add device to i2c bus, err:%d(%s)", err,
             esp_err_to_name(err));
    return err;
  }
  // 检测设备是否正确连接（通过读一个字节的状态信息判断）
  ESP_LOGD(TAG, "start probe aht20 device");

  uint8_t status = 0;
  err = aht20_status(&status);
  if (ESP_OK != err) {
    // 读取失败，说明设备未连接
    ESP_LOGE(TAG, "failed to probe aht20 device, check SDA and SCL pin");
    goto Failed;
  }
  ESP_LOGI(TAG, "aht20 device is connected");

  // 判断校准位是否为 1，不为 1 则需要发送初始化命令
  if (status & AHT20_STATUS_CAL_ENABLE) {
    ESP_LOGI(TAG,
             "success to initialize aht20 device, scl speed: %d, dev addr: %x",
             AHT20_SCL, AHT20_ADDR);
    return ESP_OK;
  }
  // init device
  if (aht20_dev_init() == ESP_OK) {
    return ESP_OK;
  }

Failed:
  i2c_master_bus_rm_device(aht20_dev_handle);
  aht20_dev_handle = NULL;
  return ESP_FAIL;
}
