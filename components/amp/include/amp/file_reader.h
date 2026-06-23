#if !defined(_AMP_FILE_READER_H_)
#define _AMP_FILE_READER_H_

#include <stdbool.h>

#include "esp_err.h"

typedef struct file_reader file_reader_handle_t;

typedef struct audio_file_source audio_file_source_handle_t;

bool file_reader_next(file_reader_handle_t *fl, audio_file_source_handle_t **cur);

esp_err_t file_reader_read_dir(file_reader_handle_t *fl, const char *dir);

#endif // _AMP_FILE_READER_H_
