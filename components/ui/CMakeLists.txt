file(GLOB U8G2_SRC_FILES "u8g2/*.c")
set(SRC_FILES "u8g2_port.c" "ui.c")

idf_component_register(SRCS ${SRC_FILES} ${U8G2_SRC_FILES}
        INCLUDE_DIRS "."
        REQUIRES esp_driver_i2c bsp button)