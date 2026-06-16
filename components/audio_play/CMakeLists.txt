set(SRC_FILES "./audio_player.c")

idf_component_register(SRCS ${SRC_FILES}
        INCLUDE_DIRS "."
        REQUIRES esp_driver_gpio esp_driver_i2s bsp)