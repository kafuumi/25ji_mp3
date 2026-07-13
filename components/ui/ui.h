#ifndef INC_UI_H
#define INC_UI_H

#include "driver/i2c_types.h"
#include "esp_err.h"

#define UI_MAIN_MEDIA_TYPE_MP3 "MP3"
#define UI_MAIN_MEDIA_TYPE_FLAC "FLAC"

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

typedef enum {
    UI_MAIN_STATE_INVALID,
    UI_MAIN_STATE_PAUSE,
    UI_MAIN_STATE_PLAY,
    UI_MAIN_STATE_FAIL,
} ui_main_state_t;

typedef struct {
    ui_main_state_t state;
    const char *media_type;
    int vol;
    const char *title;
    float sample_rate;
    int channel;
    int bit_width;
} ui_main_info_t;

void ui_main_set_info(ui_main_info_t *info);

#endif // INC_UI_H
