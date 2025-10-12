cmake_minimum_required(VERSION 3.16)

# Set component directories for Family mruby modules
set(EXTRA_COMPONENT_DIRS "main/lib" "components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fmruby-core)

# Generate SPIFFS image from spiffs_data directory
#spiffs_create_partition_image(audio spiffs_data FLASH_IN_PROJECT)
#fatfs_create_rawflash_image(storage fatfs FLASH_IN_PROJECT PRESERVE_TIME)

# TODO: Add PicoRuby component when integrated
# list(APPEND EXTRA_COMPONENT_DIRS "lib/picoruby-fmrb")
