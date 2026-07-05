#if !defined(_AMP_I2S_WRITER_H_)
#define _AMP_I2S_WRITER_H_

#include "driver/i2s_std.h"
#include "esp_err.h"

#include "amp/controller.h"

typedef struct {
    int bclk;
    int mclk;
    int dout;
    int ws;
} amp_i2s_writer_gpio_cfg_t;

typedef struct {
    i2s_port_t i2s_port;
    uint8_t volume;
    amp_i2s_writer_gpio_cfg_t gpio_cfg;
} amp_i2s_writer_cfg_t;

typedef struct i2s_writer *amp_i2s_writer_handle_t;

esp_err_t amp_i2s_writer_init(amp_i2s_writer_cfg_t *cfg, amp_i2s_writer_handle_t *writer);

void amp_i2s_writer_deinit(amp_i2s_writer_handle_t writer);

const amp_element_interface_t *amp_i2s_writer_get_element_interface(void);

void amp_i2s_writer_set_volume(amp_i2s_writer_handle_t writer, uint8_t volume);

#endif // _AMP_I2S_WRITER_H_
