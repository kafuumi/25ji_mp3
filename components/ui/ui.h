#ifndef INC_UI_H
#define INC_UI_H

#include "driver/i2c_types.h"
#include "esp_err.h"

esp_err_t ui_init(i2c_master_bus_handle_t);

#endif // INC_UI_H
