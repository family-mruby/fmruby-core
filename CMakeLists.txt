cmake_minimum_required(VERSION 3.16)

# Set component directories for Family mruby modules
set(EXTRA_COMPONENT_DIRS "components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fmruby-core)

# Generate storage image data directory (ESP32 only).
# The image is built from a staged copy of flash/, not flash/ itself, so
# runtime artifacts never ship: the Linux sim and past boots write caches
# (e.g. var/cache/launcher_index) into the working tree, and imaging flash/
# directly baked a stale dev cache into every locally flashed board. The
# var/cache directory itself must survive (with .keep): the desktop opens
# the cache file for writing without creating directories.
if(NOT IDF_TARGET STREQUAL "linux")
    set(STORAGE_STAGING ${CMAKE_BINARY_DIR}/storage_root)
    add_custom_target(storage_staging
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${STORAGE_STAGING}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/flash ${STORAGE_STAGING}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${STORAGE_STAGING}/var/cache
        COMMAND ${CMAKE_COMMAND} -E make_directory ${STORAGE_STAGING}/var/cache
        COMMAND ${CMAKE_COMMAND} -E touch ${STORAGE_STAGING}/var/cache/.keep
        COMMENT "Staging storage tree (runtime caches excluded)"
    )
    littlefs_create_partition_image(storage ${STORAGE_STAGING} FLASH_IN_PROJECT
                                    DEPENDS storage_staging)
endif()
