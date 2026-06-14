set(SRC_FILES "aht20.c")

idf_component_register(SRCS ${SRC_FILES}
        INCLUDE_DIRS "."
        REQUIRES esp_driver_i2c)