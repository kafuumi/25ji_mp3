#if !defined(_AMP_FILE_READER_H_)
#define _AMP_FILE_READER_H_

#include <stdbool.h>
#include <sys/queue.h>

#include "amp/audio_types.h"
#include "amp/element.h"
#include "esp_err.h"

typedef struct {
    const char *name;
    bool is_dir;
    enum amp_audio_media_type media_type;
} amp_audio_file_source_t;

typedef struct file_reader *amp_file_reader_handle_t;

esp_err_t amp_file_reader_init(amp_file_reader_handle_t *fr);

void amp_file_reader_deinit(amp_file_reader_handle_t fr);

amp_audio_file_source_t *amp_file_reader_next(amp_file_reader_handle_t fl);

esp_err_t amp_file_reader_read_dir(amp_file_reader_handle_t fl, const char *dir);

const amp_element_interface_t *amp_file_reader_get_element_interface(void);

#endif // _AMP_FILE_READER_H_
