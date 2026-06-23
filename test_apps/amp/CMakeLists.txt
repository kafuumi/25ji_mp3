cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(TEST_COMPONENTS "amp" CACHE STRING "List of components to test")

idf_build_set_property(MINIMAL_BUILD ON)

project(25ji_mp3_amp_test)

idf_build_set_property(COMPILE_DEFINITIONS "APP_RUN_TEST_MODE" APPEND)

fatfs_create_spiflash_image(storage test_assets FLASH_IN_PROJECT PRESERVE_TIME)