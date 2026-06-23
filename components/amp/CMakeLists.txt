set(SRC_FILES "controller.c" "i2s_writer.c"
            "sin_pcm_reader.c" "dashboard.c"
            "file_reader.c" "audio_codec.c")

idf_component_register(SRCS ${SRC_FILES}
        INCLUDE_DIRS "include"
        REQUIRES esp_event esp_ringbuf esp_driver_i2s
        PRIV_REQUIRES esp_driver_gpio esp_audio_codec bsp utils sds)
