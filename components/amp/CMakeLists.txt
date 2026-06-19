set(SRC_FILES "controller.c" "i2s_writer.c" "sin_pcm_reader.c" "dashboard.c")

idf_component_register(SRCS ${SRC_FILES}
        INCLUDE_DIRS "include"
        REQUIRES esp_driver_gpio esp_driver_i2s esp_ringbuf esp_event bsp utils)
