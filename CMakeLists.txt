cmake_minimum_required(VERSION 3.16)

# Set component directories for Family mruby modules
set(EXTRA_COMPONENT_DIRS "main/lib" "components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fmruby-core)

# Generate storage image data directory (ESP32 only)
if(NOT IDF_TARGET STREQUAL "linux")
    littlefs_create_partition_image(storage data FLASH_IN_PROJECT)
endif()

# TODO: Add PicoRuby component when integrated
# list(APPEND EXTRA_COMPONENT_DIRS "lib/picoruby-fmrb")
