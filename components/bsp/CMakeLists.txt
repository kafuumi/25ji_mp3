set(SRC_FILES "bsp.c" "bsp_sd_card.c")

idf_component_register(SRCS ${SRC_FILES}
        INCLUDE_DIRS "."
        REQUIRES esp_driver_gpio esp_driver_sdmmc esp_driver_spi fatfs)
