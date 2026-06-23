#if !defined(_AMP_FILE_READER_H_)
#define _AMP_FILE_READER_H_

#include <stdbool.h>
#include <sys/queue.h>

#include "esp_err.h"

struct audio_file_source {
    const char *name;
    bool is_dir;
};

typedef struct file_reader file_reader_handle_t;

esp_err_t file_reader_init(file_reader_handle_t **fr);

void file_reader_deinit(file_reader_handle_t *fr);

struct audio_file_source *file_reader_next(file_reader_handle_t *fl);

esp_err_t file_reader_read_dir(file_reader_handle_t *fl, const char *dir);

#endif // _AMP_FILE_READER_H_
