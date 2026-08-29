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
#
# flash/tmp is likewise dev-only: the Linux sim backs the RAM /tmp with that
# real directory, but on ESP32 /tmp is its own RAM-backed VFS. Baking a
# physical /flash/tmp made the mount point appear twice in the root listing
# (once real, once synthetic), so it is dropped from the image.
if(NOT IDF_TARGET STREQUAL "linux")
    set(STORAGE_STAGING ${CMAKE_BINARY_DIR}/storage_root)
    # Local-only overlay: flash_local/ (gitignored) is merged over flash/ so
    # personal, undistributable content (music, decks) reaches the DEVICE image
    # but can never leak into anything staged from git-tracked files (the web
    # bundle). Checked at configure time: create the directory and re-configure
    # (rake clean is enough) for it to start applying.
    if(EXISTS ${CMAKE_SOURCE_DIR}/flash_local)
        set(STORAGE_LOCAL_OVERLAY_CMD
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/flash_local ${STORAGE_STAGING})
    else()
        set(STORAGE_LOCAL_OVERLAY_CMD "")
    endif()
    add_custom_target(storage_staging
        # Settings first: flash/etc/system_conf.toml and its factory copy are
        # not in git and are staged by rake build:esp32. Building without them
        # (or with another board's) used to succeed silently and produce an
        # image that boots with no settings -- and, with no factory copy, one
        # bad save away from needing a reflash. See the script for the detail.
        COMMAND ${CMAKE_COMMAND}
                -DSRC_DIR=${CMAKE_SOURCE_DIR}
                -DIDF_TARGET=${IDF_TARGET}
                -DFMRB_HW_TARGET=${FMRB_HW_TARGET}
                -P ${CMAKE_SOURCE_DIR}/cmake/check_storage_inputs.cmake
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${STORAGE_STAGING}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/flash ${STORAGE_STAGING}
        ${STORAGE_LOCAL_OVERLAY_CMD}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${STORAGE_STAGING}/var/cache
        COMMAND ${CMAKE_COMMAND} -E make_directory ${STORAGE_STAGING}/var/cache
        COMMAND ${CMAKE_COMMAND} -E touch ${STORAGE_STAGING}/var/cache/.keep
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${STORAGE_STAGING}/tmp
        COMMENT "Staging storage tree (runtime caches excluded)"
    )
    # Services are Modern-only (doc/user_extension/services/plan.md). The
    # sample services and both lists are in the tree because the simulator and
    # the Modern image want them; a Retro image must not carry them, so they
    # come back out of the staged copy here. The files themselves stay in git.
    include(${CMAKE_SOURCE_DIR}/components/fmrb_common/fmrb_hw_defines.cmake)
    fmrb_hw_family_is_modern(FMRB_IMAGE_IS_MODERN)
    if(NOT FMRB_IMAGE_IS_MODERN)
        add_custom_command(TARGET storage_staging POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${STORAGE_STAGING}/home/services
            COMMAND ${CMAKE_COMMAND} -E rm -f  ${STORAGE_STAGING}/home/services.toml
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${STORAGE_STAGING}/usr/share/services
            COMMAND ${CMAKE_COMMAND} -E rm -f  ${STORAGE_STAGING}/etc/services.toml
            COMMENT "Retro image: leaving the service host's files out")
    endif()
    littlefs_create_partition_image(storage ${STORAGE_STAGING} FLASH_IN_PROJECT
                                    DEPENDS storage_staging)
endif()
