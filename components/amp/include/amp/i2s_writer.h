#if !defined(_AMP_I2S_WRITER_H_)
#define _AMP_I2S_WRITER_H_

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/ringbuf.h"

struct i2s_writer_output_args {
    uint32_t sample_rate; // 采样率
    i2s_slot_bit_width_t slog_bit_width;
    i2s_slot_mode_t slot_mode; // 单声道 or 双声道
};

struct i2s_writer_task_cfg {
    const char *name;
    const uint32_t stack_size;
    const UBaseType_t priority;
    const UBaseType_t core;
    TaskHandle_t *task;
};

struct i2s_writer_cfg {
    i2s_port_t i2s_port;
    RingbufHandle_t rb_in;
};

typedef struct i2s_writer i2s_writer_handle_t;

esp_err_t i2s_writer_init(struct i2s_writer_cfg *, i2s_writer_handle_t **);

void i2s_writer_deinit(i2s_writer_handle_t *writer);

esp_err_t i2s_writer_send_pcm(i2s_writer_handle_t *writer, const uint8_t *data, size_t size);

esp_err_t i2s_writer_run_task(i2s_writer_handle_t *writer, struct i2s_writer_task_cfg *cfg);

#endif // _AMP_I2S_WRITER_H_