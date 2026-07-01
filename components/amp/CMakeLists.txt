set(srcs "controller.c"
         "i2s_writer.c"
         "devnull_writer.c"
         "sin_pcm_reader.c"
         "file_reader.c"
         "playlist.c"
         "audio_decoder.c"
         "amp_mem.c"
         "ringbuf.c"
         "dashboard.c"
)

set(requires esp_event esp_driver_i2s)

set(priv_requires esp_driver_gpio esp_audio_codec bsp utils sds)

idf_component_register(SRCS ${srcs}
        INCLUDE_DIRS "include"
        REQUIRES ${requires}
        PRIV_REQUIRES ${priv_requires})
