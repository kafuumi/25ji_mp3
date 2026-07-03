#if !defined(_SENSOR_AHT20_H_)
#define _SENSOR_AHT20_H_

#include "driver/i2c_types.h"
#include "esp_err.h"

#define AHT20_ERR_BUSY -10001 // is busy
#define AHT20_ERR_CAL_DISABLE -10002

#define AHT20_ADDR 0x38

// 初始化 aht20
esp_err_t aht20_init(i2c_master_bus_handle_t bus_handle);

// 获取 aht20 状态信息
esp_err_t aht20_status(uint8_t *status);

// 日志中打印状态信息
void aht20_log_status(uint8_t status);

// 重置
esp_err_t aht20_reset();

// 读取温度和湿度
esp_err_t aht20_read_temperature_humidity(float *temperature, float *humidity);

// 读取温度
esp_err_t aht20_read_temperature(float *temperature);

// 读取湿度
esp_err_t aht20_read_humidity(float *humidity);

#endif // _SENSOR_AHT20_H