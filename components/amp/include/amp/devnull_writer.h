#if !defined(_AMP_DEVNULL_WRITER_H_)
#define _AMP_DEVNULL_WRITER_H_

#include "amp/element.h"
#include "esp_err.h"

typedef struct devnull_writer *amp_devnull_writer_handle_t;

esp_err_t amp_devnull_writer_init(amp_devnull_writer_handle_t *writer);

void amp_devnull_writer_deinit(amp_devnull_writer_handle_t writer);

const amp_element_interface_t *amp_devnull_writer_get_element_interface(void);

#endif // _AMP_DEVNULL_WRITER_H_
