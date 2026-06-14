# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "use ESP-IDF: $ENV{IDF_PATH}")

set(CMAKE_C_STANDARD 11)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(25ji_mp3)
