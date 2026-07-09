#if !defined(_AMP_AUDIO_H_)
#define _AMP_AUDIO_H_

enum amp_audio_bit_width {
    AUDIO_BIT_WIDTH_8BIT = 8,
    AUDIO_BIT_WIDTH_16BIT = 16,
    AUDIO_BIT_WIDTH_24BIT = 24,
    AUDIO_BIT_WIDTH_32BIT = 32,
};

enum amp_audio_channel {
    AUDIO_CHANNEL_MONO = 1,
    AUDIO_CHANNEL_STEREO = 2,
};

enum amp_audio_media_type {
    AUDIO_MEDIA_TYPE_NONE,
    AUDIO_MEDIA_TYPE_MP3,
    AUDIO_MEDIA_TYPE_AAC,
    AUDIO_MEDIA_TYPE_FLAC,
    AUDIO_MEDIA_TYPE_OGG,
};

#endif // _AMP_AUDIO_H_
