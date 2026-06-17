#if !defined(_ESP_UTILS_H_)
#define _ESP_UTILS_H_

#include "esp_err.h"
#define FMT_ESP_ERR(err) "%d(%s)", err, esp_err_to_name(err)

#endif // _ESP_UTILS_H_