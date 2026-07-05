set(srcs "controller.c"
         "sin_pcm_reader.c"
         "file_reader.c"
         "audio_decoder.c"
         "i2s_writer.c"
         "devnull_writer.c"
         "playlist.c"
         "amp_mem.c"
         "ringbuf.c"
         "dashboard.c"
         "element_priv.c"
)

set(requires esp_event esp_driver_i2s)

set(priv_requires esp_driver_gpio esp_audio_codec utils sds)

idf_component_register(SRCS ${srcs}
        INCLUDE_DIRS "include"
        REQUIRES ${requires}
        PRIV_REQUIRES ${priv_requires})
