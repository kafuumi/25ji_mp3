#if !defined(_AMP_I2S_WRITER_H_)
#define _AMP_I2S_WRITER_H_

#include "driver/i2s_std.h"
#include "esp_err.h"

#include "amp/controller.h"

#define AMP_I2S_WRITER_DEFAULT_OUTPUT_CONFIG()                                                                           \
    {                                                                                                                    \
        .sample_rate = 44100, .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT, .slot_mode = I2S_SLOT_MODE_STEREO             \
    }

typedef struct {
    uint32_t sample_rate;
    i2s_slot_bit_width_t slot_bit_width;
    i2s_slot_mode_t slot_mode;
} amp_i2s_writer_output_config_t;

typedef struct {
    i2s_port_t i2s_port;
} amp_i2s_writer_config_t;

typedef struct i2s_writer *amp_i2s_writer_handle_t;

esp_err_t amp_i2s_writer_init(amp_i2s_writer_config_t *cfg, amp_i2s_writer_handle_t *writer);

void amp_i2s_writer_deinit(amp_i2s_writer_handle_t writer);

esp_err_t amp_i2s_writer_set_output_config(amp_i2s_writer_handle_t writer, amp_i2s_writer_output_config_t *args);

const amp_element_interface_t *amp_i2s_writer_get_element_interface(void);

#endif // _AMP_I2S_WRITER_H_
