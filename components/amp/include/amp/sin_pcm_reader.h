#if !defined(_AMP_SIN_PCM_READER_H_)
#define _AMP_SIN_PCM_READER_H_

#include "esp_err.h"
#include "freertos/ringbuf.h"

enum sin_pcm_bit_width {
    PCM_BIT_WIDTH_8BIT,
    PCM_BIT_WIDTH_16BIT,
};

enum sin_pcm_channel {
    PCM_CHANNEL_MONO = 1,
    PCM_CHANNEL_STERO = 2,
};

struct sin_pcm_audio_args {
    int freq;
    int sample_rate;
    int volume;
    enum sin_pcm_bit_width bit_width;
    enum sin_pcm_channel channel;
};

struct sin_pcm_reader_cfg {
    int max_amplitude;
    size_t frames_size;
    RingbufHandle_t rb_out;
};

typedef struct sin_pcm_reader sin_pcm_reader_handle_t;

esp_err_t sin_pcm_reader_init(struct sin_pcm_reader_cfg *cfg, sin_pcm_reader_handle_t **reader);

esp_err_t sin_pcm_reader_run(sin_pcm_reader_handle_t *reader);

#endif // _AMP_SIN_PCM_READER_