# Standalone build of the native Linux program (doc/linux_native/, L1).
#
# The browser build's sibling: the same firmware set (hosted/hosted.cmake),
# the same cooperative FreeRTOS port, the Modern display task in the same
# process -- built with the host's gcc into one x86_64 executable, presenting
# through SDL2 instead of a page. Independent of idf.py, like wasm/; configure
# it through rake native:core (rakelib/native.rake).
#
#   fmruby-native   the program. Run it with rake native:run, or headless and
#                   driven with tools/fmrb_native.rb in the parent repository.
#
# libmruby comes from lib/add/family_mruby_native.rb (rake native:mruby --
# run it first); the Ruby bytecode and the Spinel programs from the existing
# generation steps (rake hosted:mrb via native:mrb, rake spinel:gen).

cmake_minimum_required(VERSION 3.16)
project(fmruby_native C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

# ------------------------------------------- shared with the browser build

set(FMRB_HOSTED_PLATFORM native)
set(FMRB_HOSTED_MRUBY_BUILD family-mruby-native)
set(FMRB_HOSTED_NATIVE_ISA ON)
set(FMRB_HOSTED_PTR32 OFF)
include(${CMAKE_CURRENT_SOURCE_DIR}/../hosted/hosted.cmake)

# Where the program looks by default: the repository's flash/ (the
# distribution tree, read-only) and the /etc that rake native:etc staged.
set(FMRB_NATIVE_SYSTEM_DIR ${FMRB_ROOT}/flash CACHE PATH "Default distribution tree")
set(FMRB_NATIVE_ETC_SEED ${CMAKE_CURRENT_SOURCE_DIR}/build/root/etc CACHE PATH "Default first-start /etc")
get_filename_component(FMRB_NATIVE_SYSTEM_DIR_ABS ${FMRB_NATIVE_SYSTEM_DIR} ABSOLUTE)
get_filename_component(FMRB_NATIVE_ETC_SEED_ABS ${FMRB_NATIVE_ETC_SEED} ABSOLUTE)

# ------------------------------------------------------------------ core

fmrb_hosted_core_objs(core_objs)

set(NATIVE_BACKEND_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/main_native.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/host_sdl.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/native_ctl.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/native_frame.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/png_write.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/display_backend_native.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/audio_backend_native.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/input_native.c
  ${CMAKE_CURRENT_SOURCE_DIR}/present/fmrb_present.c
  # mruby-dir's HAL over the file HAL's resolver (the ESP32 targets' file;
  # the name-only gem hal-dir-fmrb dropped the flat posix one from libmruby)
  ${FMRB_ROOT}/lib/add/hal-esp32-dir/dir_hal.c
)
target_sources(core_objs PRIVATE ${NATIVE_BACKEND_SRCS})
target_include_directories(core_objs PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/backend
  ${CMAKE_CURRENT_SOURCE_DIR}/present)
target_compile_definitions(core_objs PRIVATE
  FMRB_PLATFORM_NATIVE=1
  # The debugger listens beside the simulation's 5555
  # (FMRB_DEBUG_TCP_PORT in the environment moves it again at run time).
  FMRB_DEBUG_TCP_PORT=5565
  # Resolved host paths are absolute directories here, not "/flash/...".
  DIR_HAL_PATH_MAX=512
  FMRB_NATIVE_DEFAULT_SYSTEM_DIR="${FMRB_NATIVE_SYSTEM_DIR_ABS}"
  FMRB_NATIVE_DEFAULT_ETC_SEED="${FMRB_NATIVE_ETC_SEED_ABS}")
# Only the host side talks to SDL; everything else stays free of it.
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/host_sdl.c
  PROPERTIES COMPILE_OPTIONS "${SDL2_CFLAGS}")

add_executable(fmruby-native $<TARGET_OBJECTS:core_objs>)
target_link_libraries(fmruby-native PRIVATE
  freertos_hosted lgfx_hosted ${FMRB_HOSTED_LIBMRUBY}
  PkgConfig::SDL2 m pthread)
target_link_options(fmruby-native PRIVATE -pthread -Wl,--gc-sections)
