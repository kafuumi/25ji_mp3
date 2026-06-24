#if !defined(_AMP_AUDIO_CODEC_H_)
#define _AMP_AUDIO_CODEC_H_

#include "amp/element.h"
#include "esp_err.h"

typedef struct audio_codec *audio_codec_handle_t;

esp_err_t audio_codec_init(audio_codec_handle_t *codec);

void audio_codec_deinit(audio_codec_handle_t codec);

const amp_element_interface_t *audio_codec_el_interface();

#if defined(APP_RUN_TEST_MODE)
/* test helper */

#endif // APP_RUN_TEST_MODE

#endif // _AMP_AUDIO_CODEC_H_
