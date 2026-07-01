#if !defined(_AMP_FILE_READER_H_)
#define _AMP_FILE_READER_H_

#include "amp/audio_types.h"
#include "amp/element.h"
#include "amp/playlist.h"
#include "esp_err.h"

typedef struct {
    amp_playlist_handle_t playlist;
} amp_file_reader_cfg_t;

typedef struct file_reader *amp_file_reader_handle_t;

esp_err_t amp_file_reader_init(amp_file_reader_cfg_t *cfg, amp_file_reader_handle_t *fr);

void amp_file_reader_deinit(amp_file_reader_handle_t fr);

const amp_element_interface_t *amp_file_reader_get_element_interface(void);

#endif // _AMP_FILE_READER_H_
