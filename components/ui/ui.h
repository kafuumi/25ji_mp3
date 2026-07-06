#ifndef INC_UI_H
#define INC_UI_H

#include "driver/i2c_types.h"
#include "esp_err.h"

typedef struct {
    int task_size;
    int task_priority;
    int task_cpu_num;
    const char *task_name;
} ui_cfg_t;

esp_err_t ui_init(i2c_master_bus_handle_t);

esp_err_t ui_start(ui_cfg_t *cfg);

typedef enum {
    UI_INPUT_SELECT,
    UI_INPUT_NEXT,
    UI_INPUT_PREV,
} ui_input_event_t;

esp_err_t ui_post_input(ui_input_event_t event);

#endif // INC_UI_H
