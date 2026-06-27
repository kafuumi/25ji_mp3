#if !defined(_AMP_AUDIO_DECODER_H_)
#define _AMP_AUDIO_DECODER_H_

#include "amp/element.h"
#include "esp_err.h"

typedef struct audio_codec *amp_audio_decoder_handle_t;

esp_err_t amp_audio_decoder_init(amp_audio_decoder_handle_t *decoder);

void amp_audio_decoder_deinit(amp_audio_decoder_handle_t decoder);

const amp_element_interface_t *amp_audio_decoder_get_element_interface(void);

#endif // _AMP_AUDIO_DECODER_H_
