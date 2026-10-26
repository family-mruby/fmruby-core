# Standalone build for the WebAssembly target.
#
# Deliberately independent of idf.py: the only things this target needs from
# ESP-IDF are the FreeRTOS kernel (vendored under hosted/vendor/) and a thin
# stub of its logging/heap/clock surface (hosted/stub/), both shared with the
# native Linux build (hosted/hosted.cmake). Configure it through emcmake (see
# rakelib/wasm.rake), never through the ESP-IDF build system -- nothing here
# touches how main/ or components/ build for the ESP32 and Linux targets.
#
# Two executables:
#   poc   the P1 FreeRTOS-port proof of concept (rake wasm:poc)
#   core  the firmware itself (doc/wasm/ P4a): the Linux simulation's source
#         set with the posix link swapped for the Modern in-process link, the
#         input/display/audio backends swapped for wasm/backend/, and libmruby
#         cross-built by emcc (rake wasm:mruby -- run it first).

cmake_minimum_required(VERSION 3.16)
project(fmruby_wasm C CXX)

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "wasm/ must be configured with emcmake (see rake wasm:poc)")
endif()

set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ------------------------------------------- shared with the native build
#
# The FreeRTOS kernel and its cooperative port, LovyanGFX, and the firmware's
# source list, include paths and definitions live in hosted/hosted.cmake,
# which the native Linux build (native/) includes too.

set(FMRB_HOSTED_PLATFORM wasm)
set(FMRB_HOSTED_MRUBY_BUILD family-mruby-wasm)
set(FMRB_HOSTED_NATIVE_ISA OFF)
set(FMRB_HOSTED_PTR32 ON)
include(${CMAKE_CURRENT_SOURCE_DIR}/../hosted/hosted.cmake)

# ------------------------------------------------------------------- PoC

add_executable(poc ${CMAKE_CURRENT_SOURCE_DIR}/poc/poc_main.c)
target_link_libraries(poc PRIVATE freertos_hosted)

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

# ------------------------------------------------------------------ core
#
# The firmware: hosted/hosted.cmake's shared set plus the browser's own
# backends and entry point. The mruby ABI defines there MUST match
# lib/add/family_mruby_wasm.rb the same way mruby_abi_defines.cmake matches
# the other build_configs.

set(LIBMRUBY_WASM ${FMRB_HOSTED_LIBMRUBY})

# --- sources: wasm backends + stubs ----------------------------------------

set(WASM_BACKEND_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/main_wasm.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/page_settings_wasm.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/display_backend_wasm.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/audio_backend_wasm.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/input_wasm.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/libc_shim_wasm.c)

# ---------------------------------------------------------------------------

# --- zenoh-pico's platform layer (doc/ruby_asterism, B1) ---------------------
#
# The picoruby-asterism-zenoh gem compiles the zenoh-pico core into libmruby
# for the Emscripten platform (lib/add/family_mruby_wasm.rb); the platform
# files are ours (wasm/zenoh/: system layer and WebSocket link, without
# ASYNCIFY) plus zenoh-pico's socket stubs. Same defines and include paths as
# the gem's build, including the config.h the gem generates into the picoruby
# build tree (so libmruby must be built first, as for the rest of core).
set(ZENOH_GEM ${MRBGEMS}/picoruby-asterism-zenoh)
set(ZENOH_PICO ${ZENOH_GEM}/vendor/zenoh-pico)
set(ZENOH_WASM_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/zenoh
  ${ZENOH_GEM}/include
  ${PICORUBY_DIR}/build/family-mruby-wasm/mrbgems/picoruby-asterism-zenoh/zp_include
  ${ZENOH_PICO}/include)
set(ZENOH_WASM_DEFINES
  ZENOH_GENERIC ZENOH_EMSCRIPTEN ZENOH_C_STANDARD=11 ZENOH_COMPILER_GCC
  ZENOH_LOG_ERROR Z_FEATURE_LINK_TCP=0 Z_FEATURE_LINK_WS=1
  Z_FEATURE_UNICAST_PEER=0
  ZP_SYSTEM_PLATFORM_HEADER="zp_emscripten_platform.h")
set(ZENOH_WASM_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/zenoh/zp_system_emscripten.c
  ${CMAKE_CURRENT_SOURCE_DIR}/zenoh/zp_ws_emscripten.c
  ${ZENOH_PICO}/src/system/emscripten/network.c)
add_library(zenoh_wasm STATIC ${ZENOH_WASM_SRCS})
target_include_directories(zenoh_wasm PRIVATE ${ZENOH_WASM_INCLUDES})
target_compile_definitions(zenoh_wasm PRIVATE ${ZENOH_WASM_DEFINES} FMRB_PLATFORM_WASM=1)
target_compile_options(zenoh_wasm PRIVATE -pthread -std=gnu11)
target_link_libraries(zenoh_wasm PRIVATE freertos_hosted)

# The same layer without FreeRTOS, for the standalone probe page (C only:
# put and subscribe against zenohd's ws/ listener, wasm/zenoh/probe/).
add_library(zenoh_wasm_probe STATIC ${ZENOH_WASM_SRCS})
target_include_directories(zenoh_wasm_probe PRIVATE ${ZENOH_WASM_INCLUDES})
target_compile_definitions(zenoh_wasm_probe PRIVATE ${ZENOH_WASM_DEFINES})
target_compile_options(zenoh_wasm_probe PRIVATE -pthread -std=gnu11)
add_executable(zenoh_probe ${CMAKE_CURRENT_SOURCE_DIR}/zenoh/probe/zenoh_probe.c)
target_include_directories(zenoh_probe PRIVATE ${ZENOH_WASM_INCLUDES})
target_compile_definitions(zenoh_probe PRIVATE ${ZENOH_WASM_DEFINES})
target_compile_options(zenoh_probe PRIVATE -pthread -std=gnu11)
# The zenoh-pico core comes out of libmruby (the gem's objects); only what
# the probe calls is pulled in.
target_link_libraries(zenoh_probe PRIVATE zenoh_wasm_probe ${LIBMRUBY_WASM} zenoh_wasm_probe)
target_link_options(zenoh_probe PRIVATE
  -pthread -sPROXY_TO_PTHREAD -sENVIRONMENT=web,worker -sEXIT_RUNTIME=1
  -sALLOW_MEMORY_GROWTH)
set_target_properties(zenoh_probe PROPERTIES EXCLUDE_FROM_ALL TRUE)

# One compile of the firmware objects, two links: `core` for node (real
# files via NODERAWFS, the headless verification loop) and `core_web` for the
# browser (MEMFS preloaded with flash/, modularized for the page).
fmrb_hosted_core_objs(core_objs)
target_sources(core_objs PRIVATE ${WASM_BACKEND_SRCS})
target_include_directories(core_objs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backend)
target_compile_definitions(core_objs PRIVATE FMRB_PLATFORM_WASM=1)

add_executable(core $<TARGET_OBJECTS:core_objs>)
target_link_libraries(core PRIVATE freertos_hosted lgfx_hosted ${LIBMRUBY_WASM} zenoh_wasm)

add_executable(core_web $<TARGET_OBJECTS:core_objs>)
target_link_libraries(core_web PRIVATE freertos_hosted lgfx_hosted ${LIBMRUBY_WASM} zenoh_wasm)
set_target_properties(core_web PROPERTIES EXCLUDE_FROM_ALL TRUE)

set(WASM_CORE_LINK_OPTS
  -pthread
  -sPROXY_TO_PTHREAD
  # One Worker per FreeRTOS task. 32 app slots plus the resident tasks
  # (kernel, host, desktop, overlay, services, display, audio, input, log,
  # transport, ...) -- prewarmed, because a pthread_create that finds the pool
  # empty only completes once the main thread gets a turn.
  -sPTHREAD_POOL_SIZE=56
  -sALLOW_MEMORY_GROWTH
  -sINITIAL_MEMORY=268435456
  -sMAXIMUM_MEMORY=1073741824
  -sSTACK_SIZE=1048576
  -sEXIT_RUNTIME=1
  # The JS-facing surface (frame, audio ring, input ring); emscripten 6
  # exports nothing to the Module by default.
  "-sEXPORTED_FUNCTIONS=_main,_fmrb_wasm_frame_seq,_fmrb_wasm_frame_rgba,_fmrb_wasm_frame_width,_fmrb_wasm_frame_height,_fmrb_wasm_audio_ring,_fmrb_wasm_audio_ring_size,_fmrb_wasm_audio_wr,_fmrb_wasm_audio_rate,_fmrb_wasm_input_ring,_fmrb_wasm_input_ring_events,_fmrb_wasm_input_wr_ptr"
  # FS is exported for the storage work (doc/wasm/instruction_storage.md):
  # the page has to create and mount /flash/home before the machine boots.
  # Settings still travel as argv and are applied by page_settings_wasm.c on
  # the machine's side -- burning them in there is the surer order.
  "-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32,HEAP32,FS"
)

# Node development build: real files (the flash/ directory) instead of a
# packed MEMFS image; the browser build swaps this for --preload-file (P4b).
target_link_options(core PRIVATE
  ${WASM_CORE_LINK_OPTS}
  -sENVIRONMENT=node
  -sNODERAWFS=1
  # Development build: keep function names in traces and check at runtime.
  -sASSERTIONS=1
  --profiling-funcs
)

# Browser build: MEMFS with the flash image packed in, modularized so the
# page (wasm/web/) instantiates it explicitly. COOP/COEP headers are the
# server's job (rake wasm:serve for development).
#
# The packed tree is NOT the working flash/ directory: rake wasm:web stages
# build/webflash from git-TRACKED flash/ content plus config/system_conf_wasm
# .toml, so local-only files (flash_local/ overlay material, the generated
# wifi.toml, anything untracked) can never reach the distributed bundle.
target_link_options(core_web PRIVATE
  ${WASM_CORE_LINK_OPTS}
  -sENVIRONMENT=web,worker
  -sMODULARIZE=1
  -sEXPORT_NAME=createFmrbCore
  --preload-file ${CMAKE_BINARY_DIR}/webflash@/flash
  # /home is mounted on IndexedDB by the page so the user's files outlive the
  # tab (doc/wasm/instruction_storage.md T2). Not linked into the node build,
  # which has real files through NODERAWFS.
  -lidbfs.js
  --profiling-funcs
)
