# Standalone build for the WebAssembly target.
#
# Deliberately independent of idf.py: the only thing this target needs from
# ESP-IDF is the FreeRTOS kernel, and that is vendored under vendor/. Configure
# it through emcmake (see rakelib/wasm.rake), never through the ESP-IDF build
# system -- nothing here touches main/ or components/, so the ESP32 and Linux
# builds are unaffected.
#
# P1 builds the port and its proof of concept only. P4a adds the core sources.

cmake_minimum_required(VERSION 3.16)
project(fmruby_wasm C)

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "wasm/ must be configured with emcmake (see rake wasm:poc)")
endif()

set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --------------------------------------------------------------- FreeRTOS

set(FREERTOS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/freertos)
set(FREERTOS_IDF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/freertos_idf)

add_library(freertos_wasm STATIC
  ${FREERTOS_DIR}/tasks.c
  ${FREERTOS_DIR}/queue.c
  ${FREERTOS_DIR}/list.c
  ${FREERTOS_DIR}/stream_buffer.c
  ${CMAKE_CURRENT_SOURCE_DIR}/port/port.c
  ${CMAKE_CURRENT_SOURCE_DIR}/port/wait_for_event.c
  ${CMAKE_CURRENT_SOURCE_DIR}/stub/heap_wasm.c
)

target_include_directories(freertos_wasm PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/port
  ${CMAKE_CURRENT_SOURCE_DIR}/stub/include
  ${FREERTOS_DIR}/include
  ${FREERTOS_DIR}/include/freertos
  ${FREERTOS_IDF_DIR}
  ${FREERTOS_IDF_DIR}/include
)

# The vendored kernel is ESP-IDF's, warts and all; do not let its style choices
# drown out warnings from our own code.
target_compile_options(freertos_wasm PRIVATE -Wno-c23-extensions -Wno-unused-value)
target_compile_options(freertos_wasm PUBLIC -pthread)

# ------------------------------------------------------------------- PoC

add_executable(poc ${CMAKE_CURRENT_SOURCE_DIR}/poc/poc_main.c)
target_link_libraries(poc PRIVATE freertos_wasm)

# Prewarmed Workers. Deliberately smaller than the number of tasks the PoC
# creates, so the exhaustion path gets exercised on every run; lower it
# (-DWASM_PTHREAD_POOL_SIZE=4) to see the cost of running dry more sharply.
set(WASM_PTHREAD_POOL_SIZE 32 CACHE STRING "Emscripten pthread pool size")

# PROXY_TO_PTHREAD moves main() off the browser's main thread, which a port
# that blocks in xPortStartScheduler() has to have; it is also what lets
# pthread_create() finish when the pool is empty (the main thread stays free to
# service the request).
target_link_options(poc PRIVATE
  -pthread
  -sPROXY_TO_PTHREAD
  -sPTHREAD_POOL_SIZE=${WASM_PTHREAD_POOL_SIZE}
  -sALLOW_MEMORY_GROWTH
  -sINITIAL_MEMORY=134217728
  -sMAXIMUM_MEMORY=1073741824
  -sSTACK_SIZE=1048576
  -sEXIT_RUNTIME=1
  -sENVIRONMENT=node
)
