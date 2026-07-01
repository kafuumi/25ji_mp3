#if !defined(_AMP_SIN_PCM_READER_H_)
#define _AMP_SIN_PCM_READER_H_

#include "esp_err.h"

#include "amp/audio_types.h"
#include "amp/controller.h"

typedef struct {
    int freq;
    int sample_rate;
    int volume;
    enum amp_audio_bit_width bit_width;
    enum amp_audio_channel channel;
} amp_sine_pcm_audio_config_t;

typedef struct {
    int max_amplitude;
    size_t frames_size;
} amp_sine_pcm_reader_cfg_t;

typedef struct sin_pcm_reader *amp_sine_pcm_reader_handle_t;

esp_err_t amp_sine_pcm_reader_init(amp_sine_pcm_reader_cfg_t *cfg, amp_sine_pcm_reader_handle_t *reader);

void amp_sine_pcm_reader_set_audio_config(amp_sine_pcm_reader_handle_t reader, const amp_sine_pcm_audio_config_t *args);

const amp_element_interface_t *amp_sine_pcm_reader_get_element_interface(void);

#endif // _AMP_SIN_PCM_READER_H_
