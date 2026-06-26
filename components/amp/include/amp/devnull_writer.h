#if !defined(_AMP_DEVNULL_WRITER_H_)
#define _AMP_DEVNULL_WRITER_H_

#include "amp/element.h"
#include "esp_err.h"

typedef struct devnull_writer *devnull_writer_handle_t;

esp_err_t devnull_writer_init(devnull_writer_handle_t *writer);

void devnull_writer_deinit(devnull_writer_handle_t writer);

const amp_element_interface_t *devnull_writer_el_interface();

#endif // _AMP_DEVNULL_WRITER_H_
