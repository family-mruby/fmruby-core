# Standalone build for the WebAssembly target.
#
# Deliberately independent of idf.py: the only things this target needs from
# ESP-IDF are the FreeRTOS kernel (vendored under vendor/) and a thin stub of
# its logging/heap/clock surface (stub/). Configure it through emcmake (see
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

# ------------------------------------------------------------------ core
#
# The firmware, assembled the way doc/wasm/implementation_plan.md P4a says:
# the Linux build's compile_commands.json file set is the baseline, minus the
# posix link (fmrb_hal_link_local instead), minus the linux input/debugd/sim
# pieces (wasm/backend/ + exclusions), plus the wasm display sink. The mruby
# ABI defines below MUST match lib/add/family_mruby_wasm.rb the same way
# mruby_abi_defines.cmake matches the other build_configs.

set(FMRB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(PICORUBY_DIR ${FMRB_ROOT}/components/picoruby-esp32/picoruby)
set(MRBGEMS ${PICORUBY_DIR}/mrbgems)
set(LIBMRUBY_WASM ${PICORUBY_DIR}/build/family-mruby-wasm/lib/libmruby.a)

if(NOT EXISTS ${LIBMRUBY_WASM})
  message(FATAL_ERROR "libmruby for wasm not found (${LIBMRUBY_WASM}); run `rake wasm:mruby` first")
endif()

# --- LovyanGFX (sprite/font core only; rake wasm:setup fetches + patches) --

set(M5GFX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/m5gfx/src)
if(NOT EXISTS ${M5GFX_SRC}/M5GFX.h)
  message(FATAL_ERROR "M5GFX not found (${M5GFX_SRC}); run `rake wasm:setup` first")
endif()

add_library(lgfx_wasm STATIC
  ${M5GFX_SRC}/lgfx/v1/LGFXBase.cpp
  ${M5GFX_SRC}/lgfx/v1/LGFX_Sprite.cpp
  ${M5GFX_SRC}/lgfx/v1/lgfx_fonts.cpp
  ${M5GFX_SRC}/lgfx/v1/misc/SpriteBuffer.cpp
  ${M5GFX_SRC}/lgfx/v1/misc/common_function.cpp
  ${M5GFX_SRC}/lgfx/v1/misc/pixelcopy.cpp
  ${M5GFX_SRC}/lgfx/v1/misc/DividedFrameBuffer.cpp
  ${M5GFX_SRC}/lgfx/v1/panel/Panel_Device.cpp
  ${M5GFX_SRC}/lgfx/utility/lgfx_pngle.c
  ${M5GFX_SRC}/lgfx/utility/lgfx_qoi.c
  ${M5GFX_SRC}/lgfx/utility/lgfx_tjpgd.c
  ${M5GFX_SRC}/lgfx/utility/lgfx_miniz.c
  ${M5GFX_SRC}/lgfx/utility/lgfx_qrcode.c
  ${M5GFX_SRC}/lgfx/Fonts/efont/lgfx_efont_ja.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/lgfx_common_wasm.cpp
)
target_include_directories(lgfx_wasm PUBLIC ${M5GFX_SRC})
# The platform branch rake wasm:setup patched in; PUBLIC so every consumer's
# view of the lgfx headers picks the same (wasm) platform, ahead of the
# ESP_PLATFORM define the rest of the core build carries.
target_compile_definitions(lgfx_wasm PUBLIC FMRB_LGFX_WASM)
target_compile_options(lgfx_wasm PRIVATE -pthread -w)

# --- sources: components ---------------------------------------------------

set(CORE_SRCS
  # fmrb_common
  ${FMRB_ROOT}/components/fmrb_common/src/fmrb_debug.c
  ${FMRB_ROOT}/components/fmrb_common/src/fmrb_sysinfo.c
  ${FMRB_ROOT}/components/fmrb_common/src/fmrb_task.c
  # mem_allocator + fmrb_mem
  ${FMRB_ROOT}/components/mem_allocator/fmrb_tlsf.c
  ${FMRB_ROOT}/components/fmrb_mem/fmrb_alloc.c
  ${FMRB_ROOT}/components/fmrb_mem/fmrb_mempool.c
  # fmrb_log
  ${FMRB_ROOT}/components/fmrb_log/fmrb_log.c
  ${FMRB_ROOT}/components/fmrb_log/fmrb_log_buffer.c
  # fmrb_msg / cobs / transport
  ${FMRB_ROOT}/components/fmrb_msg/fmrb_msg.c
  ${FMRB_ROOT}/components/fmrb_msg/fmrb_hid_event.c
  ${FMRB_ROOT}/components/fmrb_cobs/fmrb_link_cobs.c
  ${FMRB_ROOT}/components/fmrb_transport/fmrb_transport.c
  ${FMRB_ROOT}/components/fmrb_transport/fmrb_transport_fragment.c
  # toml
  ${FMRB_ROOT}/components/toml_parser/tomlc99/toml.c
  ${FMRB_ROOT}/components/fmrb_toml/fmrb_toml.c
  # gfx / audio (transport side)
  ${FMRB_ROOT}/components/fmrb_gfx/fmrb_gfx.c
  ${FMRB_ROOT}/components/fmrb_gfx/fmrb_gfx_cmd.c
  ${FMRB_ROOT}/components/fmrb_gfx/fmrb_app_canvas.c
  ${FMRB_ROOT}/components/fmrb_audio/fmrb_audio.c
  ${FMRB_ROOT}/components/fmrb_audio/fmrb_wav.c
  # tar
  ${FMRB_ROOT}/components/tar_lib/microtar/src/microtar.c
  # msgpack
  ${FMRB_ROOT}/components/msgpack-esp32/msgpack-c/src/unpack.c
  ${FMRB_ROOT}/components/msgpack-esp32/msgpack-c/src/objectc.c
  ${FMRB_ROOT}/components/msgpack-esp32/msgpack-c/src/version.c
  ${FMRB_ROOT}/components/msgpack-esp32/msgpack-c/src/vrefbuffer.c
  ${FMRB_ROOT}/components/msgpack-esp32/msgpack-c/src/zone.c
  # fmrb_hal: common + posix, with the local Message Buffer link in place of
  # the posix (shared-memory) link -- the Modern in-process display path.
  ${FMRB_ROOT}/components/fmrb_hal/fmrb_hal.c
  ${FMRB_ROOT}/components/fmrb_hal/fmrb_hal_time.c
  ${FMRB_ROOT}/components/fmrb_hal/platform/posix/fmrb_hal_gpio_posix.c
  ${FMRB_ROOT}/components/fmrb_hal/platform/posix/fmrb_hal_i2c_posix.c
  ${FMRB_ROOT}/components/fmrb_hal/platform/posix/fmrb_hal_rmt_posix.c
  ${FMRB_ROOT}/components/fmrb_hal/platform/posix/fmrb_hal_pin_manager_posix.c
  ${FMRB_ROOT}/components/fmrb_hal/platform/posix/fmrb_hal_spi_posix.c
  ${FMRB_ROOT}/components/fmrb_hal/platform/posix/fmrb_hal_file_posix.c
  ${FMRB_ROOT}/components/fmrb_hal/platform/posix/fmrb_tmpfs_posix.c
  ${FMRB_ROOT}/components/fmrb_hal/platform/posix/fmrb_hal_uart_posix.c
  ${FMRB_ROOT}/components/fmrb_hal/platform/esp32/fmrb_hal_link_local.c
)

# lua (the component's explicit list: the directory also holds the
# amalgamation, the standalone interpreter and the test scaffold)
file(GLOB LUA_SRCS ${FMRB_ROOT}/components/lua/lua/*.c)
list(REMOVE_ITEM LUA_SRCS
  ${FMRB_ROOT}/components/lua/lua/onelua.c
  ${FMRB_ROOT}/components/lua/lua/lua.c
  ${FMRB_ROOT}/components/lua/lua/ltests.c)
list(APPEND CORE_SRCS
  ${LUA_SRCS}
  ${FMRB_ROOT}/components/lua/fmrb_lua.c
  ${FMRB_ROOT}/components/lua/extension/fmrb_lua_gfx.c)

# basic (C++)
list(APPEND CORE_SRCS
  ${FMRB_ROOT}/components/basic/core/basic_core.cpp
  ${FMRB_ROOT}/components/basic/core/basic_charset.cpp
  ${FMRB_ROOT}/components/basic/core/basic_tokenizer.cpp
  ${FMRB_ROOT}/components/basic/core/basic_vars.cpp
  ${FMRB_ROOT}/components/basic/core/basic_expr.cpp
  ${FMRB_ROOT}/components/basic/core/basic_exec.cpp
  ${FMRB_ROOT}/components/basic/core/basic_screen.cpp
  ${FMRB_ROOT}/components/basic/core/basic_sprite.cpp
  ${FMRB_ROOT}/components/basic/core/basic_mml.cpp
  ${FMRB_ROOT}/components/basic/fmrb_basic.cpp
  ${FMRB_ROOT}/components/basic/extension/fmrb_basic_gfx.c
  ${FMRB_ROOT}/components/basic/assets/basic_assets.c
  ${FMRB_ROOT}/components/basic/assets/basic_font8.c
  ${FMRB_ROOT}/components/basic/assets/basic_tile_a.c)

# micropython (the embed port's generated list is relative to its component)
include(${FMRB_ROOT}/components/micropython/mp_embed_srcs.cmake)
list(TRANSFORM MP_EMBED_SRCS PREPEND ${FMRB_ROOT}/components/micropython/)
list(APPEND CORE_SRCS
  ${MP_EMBED_SRCS}
  ${FMRB_ROOT}/components/micropython/fmrb_mp.c
  ${FMRB_ROOT}/components/micropython/port/mpport.c
  ${FMRB_ROOT}/components/micropython/modules/fmrb_module.c
  ${FMRB_ROOT}/components/micropython/modules/fmrb_bridge.c)

# Spinel runtime: whole snapshot minus the coroutine pair, same as the ESP32
# targets (SP_NO_MMAN, doc/wasm/ P4a).
file(GLOB SPINEL_RT_SRCS ${FMRB_ROOT}/components/fmrb_spinel_rt/spinel_rt/*.c)
list(REMOVE_ITEM SPINEL_RT_SRCS
  ${FMRB_ROOT}/components/fmrb_spinel_rt/spinel_rt/sp_fiber.c
  ${FMRB_ROOT}/components/fmrb_spinel_rt/spinel_rt/sp_sched.c)
list(APPEND CORE_SRCS
  ${SPINEL_RT_SRCS}
  ${FMRB_ROOT}/components/fmrb_spinel_rt/fmrb_spinel_host.c)

# picoruby gem ports (the linux set: esp32 port dirs are target independent
# where they matter, the FreeRTOS-dependent ones compile against our port)
foreach(gem fmrb-app fmrb-kernel fmrb-const fmrb-log fmrb-io fmrb-filesystem
        fmrb-msgpack fmrb-bmp332 fmrb-fft fmrb-spinel-hello fmrb-raycast
        fmrb-debug fmrb-editor-core fmrb-midi)
  file(GLOB GEM_PORT_SRCS ${MRBGEMS}/picoruby-${gem}/ports/esp32/*.c)
  list(APPEND CORE_SRCS ${GEM_PORT_SRCS})
endforeach()
list(APPEND CORE_SRCS
  ${MRBGEMS}/picoruby-fmrb-bmp332/src/fmrb_bmp332.c
  ${MRBGEMS}/picoruby-mruby/lib/mruby/mrbgems/mruby-task/ports/freertos/task_hal.c
  ${MRBGEMS}/picoruby-machine/ports/common/machine.c
  ${MRBGEMS}/picoruby-rng/ports/posix/rng.c)

# apu emulator (Modern's local audio synthesis; output goes to the wasm ring)
list(APPEND CORE_SRCS
  ${FMRB_ROOT}/components/apu_emu/src/apu_if_p4.c
  ${FMRB_ROOT}/components/apu_emu/src/apu_helper.c
  ${FMRB_ROOT}/components/apu_emu/src/nofrendo/nes_apu.c
  ${FMRB_ROOT}/components/apu_emu/src/nes_cpu.c
  ${FMRB_ROOT}/components/apu_emu/src/nsf_player.c
  ${FMRB_ROOT}/components/apu_emu/src/fmsq_player.c)

# --- sources: main ---------------------------------------------------------

list(APPEND CORE_SRCS
  ${FMRB_ROOT}/main/main.c
  ${FMRB_ROOT}/main/boot/boot.c
  ${FMRB_ROOT}/main/app/fmrb_app.c
  ${FMRB_ROOT}/main/app/fmrb_app_spawner.c
  ${FMRB_ROOT}/main/kernel/fmrb_kernel.c
  ${FMRB_ROOT}/main/kernel/fmrb_rtc.c
  ${FMRB_ROOT}/main/kernel/host/host_task.c
  # Modern's "GA storage is our own filesystem" file receiver; the wasm build
  # has the same one-process shape (host_task.c guards widened accordingly).
  ${FMRB_ROOT}/main/kernel/host/host_file_local.c
  ${FMRB_ROOT}/main/drivers/usb/fmrb_keymap.c
  ${FMRB_ROOT}/main/drivers/usb/fmrb_kana.c
  ${FMRB_ROOT}/main/drivers/led_status/status_led.c
  ${FMRB_ROOT}/main/drivers/debug/fmrb_debug_proto.c
  ${FMRB_ROOT}/main/drivers/debug/fmrb_debug_ctx.c
  # debugd is compiled (the editor's on-device debugger takes the local
  # session through it) but never started: boot.c skips fmrb_debugd_init on
  # wasm, so the TCP transport below is dead weight the linker keeps cheaply.
  ${FMRB_ROOT}/main/drivers/debug/fmrb_debugd.c
  ${FMRB_ROOT}/main/drivers/debug/fmrb_debug_transport_tcp.c
  ${FMRB_ROOT}/main/conn_check/fs_bench.c
  # the Modern display task, sprite-only under FMRB_PLATFORM_WASM
  ${FMRB_ROOT}/main/drivers/display_p4/display_p4_task.cpp
  ${FMRB_ROOT}/main/drivers/display_p4/display_p4_vm.cpp
  ${FMRB_ROOT}/main/drivers/display_p4/display_p4_sprite.cpp
  ${FMRB_ROOT}/main/drivers/display_p4/display_backend.cpp
  ${FMRB_ROOT}/main/drivers/display_p4/display_blend_cpu.c
  ${FMRB_ROOT}/main/drivers/display_p4/fonts/misaki/lgfx_misaki.c
  ${FMRB_ROOT}/main/drivers/display_p4/fonts/misaki/lgfx_misaki.cpp
  # the Modern audio pipeline over the wasm ring backend
  ${FMRB_ROOT}/main/drivers/audio_p4/audio_p4_task.c
  ${FMRB_ROOT}/main/drivers/audio_p4/audio_p4_handler.c
  ${FMRB_ROOT}/lib/add/picoruby-fmrb-fft/native/fmrb_fft_bench.c
  ${FMRB_ROOT}/lib/add/picoruby-fmrb-fft/native/fmrb_fft_spinel.c
  ${FMRB_ROOT}/lib/add/picoruby-fmrb-spinel-hello/native/spinel_hello_native.c
  ${FMRB_ROOT}/lib/add/picoruby-fmrb-raycast/native/raycast_native.c
  # engine shims: standard config = Spinel kernel + Spinel editor, mruby desktop
  ${FMRB_ROOT}/main/kernel/fmrb_spx_common.c
  ${FMRB_ROOT}/main/kernel/fmrb_spx_kernel.c
  ${FMRB_ROOT}/main/app/fmrb_spx_gfx.c
  ${FMRB_ROOT}/main/app/fmrb_spx_app.c
  ${FMRB_ROOT}/main/app/fmrb_spx_editor.c)

# Ruby bytecode (mrbc output, target independent) and the host-generated
# Spinel programs. Both are produced by the Linux build / rake spinel:gen and
# reused as-is; regenerating them is not this build's job.
file(GLOB KERNEL_MRB_SRCS ${FMRB_ROOT}/main/prebuild_scripts/kernel/mrb/*.c)
file(GLOB APP_MRB_SRCS ${FMRB_ROOT}/main/prebuild_scripts/default_app/mrb/*.c)
list(APPEND CORE_SRCS ${KERNEL_MRB_SRCS} ${APP_MRB_SRCS})
set(SPINEL_GEN ${FMRB_ROOT}/main/prebuild_scripts/spinel/gen)
set(SPINEL_GEN_SRCS
  ${SPINEL_GEN}/fmrb_kernel_combined.c
  ${SPINEL_GEN}/editor_combined.c
  ${SPINEL_GEN}/fft_spinel.c
  ${SPINEL_GEN}/spinel_hello_entry.c
  ${SPINEL_GEN}/raycast_entry.c)
list(APPEND CORE_SRCS ${SPINEL_GEN_SRCS})

# --- sources: wasm backends + stubs ----------------------------------------

list(APPEND CORE_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/main_wasm.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/display_backend_wasm.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/display_video_stub.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/rd_encoder_stub.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/audio_backend_wasm.c
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/input_wasm.c
  ${CMAKE_CURRENT_SOURCE_DIR}/stub/esp_stub.c)

# ---------------------------------------------------------------------------

# One compile of the firmware objects, two links: `core` for node (real
# files via NODERAWFS, the headless verification loop) and `core_web` for the
# browser (MEMFS preloaded with flash/, modularized for the page).
add_library(core_objs OBJECT ${CORE_SRCS})
target_link_libraries(core_objs PUBLIC lgfx_wasm freertos_wasm)

add_executable(core $<TARGET_OBJECTS:core_objs>)
target_link_libraries(core PRIVATE freertos_wasm lgfx_wasm ${LIBMRUBY_WASM})

add_executable(core_web $<TARGET_OBJECTS:core_objs>)
target_link_libraries(core_web PRIVATE freertos_wasm lgfx_wasm ${LIBMRUBY_WASM})
set_target_properties(core_web PROPERTIES EXCLUDE_FROM_ALL TRUE)

target_include_directories(core_objs PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/backend
  ${FMRB_ROOT}/components/fmrb_common/include
  ${FMRB_ROOT}/components/fmrb_hal
  ${FMRB_ROOT}/components/fmrb_hal/platform
  ${FMRB_ROOT}/components/fmrb_hal/platform/esp32
  ${FMRB_ROOT}/components/fmrb_hal/hw_proxy
  ${FMRB_ROOT}/components/fmrb_log
  ${FMRB_ROOT}/components/fmrb_mem
  ${FMRB_ROOT}/components/fmrb_msg
  ${FMRB_ROOT}/components/fmrb_cobs
  ${FMRB_ROOT}/components/fmrb_toml
  ${FMRB_ROOT}/components/toml_parser/tomlc99
  ${FMRB_ROOT}/components/fmrb_transport
  ${FMRB_ROOT}/components/fmrb_gfx
  ${FMRB_ROOT}/components/fmrb_audio
  ${FMRB_ROOT}/components/mem_allocator/include
  ${FMRB_ROOT}/components/mem_allocator
  ${FMRB_ROOT}/components/tar_lib/microtar/src
  ${FMRB_ROOT}/components/msgpack-esp32/msgpack-c/include
  ${FMRB_ROOT}/components/msgpack-esp32/include
  ${FMRB_ROOT}/components/lua/include
  ${FMRB_ROOT}/components/lua/lua
  ${FMRB_ROOT}/components/lua/extension
  ${FMRB_ROOT}/components/basic/include
  ${FMRB_ROOT}/components/basic/core
  ${FMRB_ROOT}/components/basic/extension
  ${FMRB_ROOT}/components/basic/assets
  ${FMRB_ROOT}/components/micropython/include
  ${FMRB_ROOT}/components/micropython/mp_embed
  ${FMRB_ROOT}/components/micropython/port
  ${FMRB_ROOT}/components/micropython/modules
  ${FMRB_ROOT}/components/micropython/prelude
  ${FMRB_ROOT}/components/fmrb_spinel_rt/spinel_rt
  ${FMRB_ROOT}/components/fmrb_spinel_rt/include
  # picoruby (same list as components/picoruby-esp32/CMakeLists.txt)
  ${FMRB_ROOT}/components/picoruby-esp32
  ${PICORUBY_DIR}/include
  ${MRBGEMS}/picoruby-machine/include
  ${MRBGEMS}/picoruby-io-console/include
  ${MRBGEMS}/picoruby-mbedtls/include
  ${MRBGEMS}/picoruby-fmrb-const/include
  ${MRBGEMS}/picoruby-fmrb-bmp332/include
  ${MRBGEMS}/picoruby-fmrb-midi/include
  ${MRBGEMS}/picoruby-fmrb-app/include
  ${MRBGEMS}/picoruby-fmrb-app/ports
  ${MRBGEMS}/picoruby-fmrb-msgpack/include
  ${MRBGEMS}/picoruby-fmrb-debug/include
  ${MRBGEMS}/mruby-compiler/include
  ${MRBGEMS}/mruby-compiler/lib/prism/include
  ${MRBGEMS}/picoruby-ti/include
  ${MRBGEMS}/picoruby-mruby/lib/mruby/include
  ${MRBGEMS}/picoruby-mruby/include
  ${MRBGEMS}/picoruby-mruby/lib/mruby/mrbgems/mruby-task/include
  ${MRBGEMS}/picoruby-mruby/lib/mruby/mrbgems/mruby-dir/include
  ${MRBGEMS}/picoruby-gpio/include
  ${MRBGEMS}/picoruby-i2c/include
  ${MRBGEMS}/picoruby-rmt/include
  ${PICORUBY_DIR}/build/family-mruby-wasm/include
  ${PICORUBY_DIR}/build/family-mruby-wasm/mrbgems
  # main
  ${FMRB_ROOT}/main/include
  ${FMRB_ROOT}/main/boot
  ${FMRB_ROOT}/main/kernel
  ${FMRB_ROOT}/main/kernel/include
  ${FMRB_ROOT}/main/kernel/host
  ${FMRB_ROOT}/lib/add/picoruby-fmrb-fft/native
  ${FMRB_ROOT}/lib/add/picoruby-fmrb-spinel-hello/native
  ${FMRB_ROOT}/lib/add/picoruby-fmrb-raycast/native
  ${FMRB_ROOT}/main/drivers/led_status
  ${FMRB_ROOT}/main/drivers/usb
  ${FMRB_ROOT}/main/drivers/debug
  ${FMRB_ROOT}/main/conn_check
  ${FMRB_ROOT}/main/drivers/display_p4
  ${FMRB_ROOT}/main/drivers/audio_p4
  ${FMRB_ROOT}/main/drivers/remote_desktop
  ${FMRB_ROOT}/components/apu_emu/include
  ${FMRB_ROOT}/components/apu_emu/src
  ${FMRB_ROOT}/components/apu_emu/src/nofrendo
)

target_compile_definitions(core_objs PRIVATE
  # Also a compile definition (not only stub sdkconfig.h): many fmrb sources
  # test CONFIG_IDF_TARGET_LINUX before any header chain reaches sdkconfig.h.
  CONFIG_IDF_TARGET_LINUX=1
  # IDF defines ESP_PLATFORM on every target (linux included); the vendored
  # FreeRTOS.h only pulls freertos/idf_additions.h (xTaskCreate, the TLSP
  # delete-callback API) under it.
  ESP_PLATFORM=1
  FMRB_PLATFORM_WASM=1
  FMRB_HW_FAMILY_MODERN
  PICORB_PLATFORM_POSIX
  PICORB_VM_MRUBY=1
  # mruby ABI -- keep in step with lib/add/family_mruby_wasm.rb
  MRB_INT64=1
  MRB_NO_BOXING
  MRB_UTF8_STRING
  MRB_USE_TASK_SCHEDULER
  MRB_TICK_UNIT=5
  MRB_TIMESLICE_TICK_COUNT=10
  MRB_USE_DEBUG_HOOK
  MRB_32BIT
  MRB_BASELINE_PROFILE=1
  MRB_TASK_TICK_SELF_SUPPLY
  # engines: the standard config (Spinel kernel + Spinel editor)
  FMRB_KERNEL_ENGINE_SPINEL
  FMRB_APP_ENGINE_EDITOR_SPINEL
  FMRB_FFT_SPINEL
  # Spinel runtime sizing, both sides (runtime TUs and generated TUs)
  SP_GC_STACK_MAX=8192
  SP_DYN_SYMS_MAX=256
  SP_EXC_STACK_MAX=16
  SP_CATCH_STACK_MAX=16
  SP_GC_MARK_STACK_MAX=8192
  # MicroPython: no native emitters on wasm, NLR via setjmp
  MICROPY_NLR_SETJMP=1
)

# Spinel: runtime TUs and the generated program TUs both build with the
# MMU-less port flags and the multi-instance allocation override, exactly as
# fmrb_spinel_rt/CMakeLists.txt + main/CMakeLists.txt do for the ESP32 targets.
set(WASM_SPINEL_TU_FLAGS
  "-w;-DSP_MULTI_CTX;-DSP_NO_MMAN;-DSP_STACK_SCRATCH_MAX=512;-include;${FMRB_ROOT}/components/fmrb_spinel_rt/fmrb_sp_tu_bss.h;-include;${FMRB_ROOT}/components/fmrb_spinel_rt/spinel_rt/sp_mem_override.h")
set_source_files_properties(${SPINEL_RT_SRCS}
  ${FMRB_ROOT}/components/fmrb_spinel_rt/fmrb_spinel_host.c
  ${SPINEL_GEN_SRCS}
  PROPERTIES COMPILE_OPTIONS "${WASM_SPINEL_TU_FLAGS}")

target_compile_options(core_objs PRIVATE -pthread -Wno-format)

set(WASM_CORE_LINK_OPTS
  -pthread
  -sPROXY_TO_PTHREAD
  -sPTHREAD_POOL_SIZE=40
  -sALLOW_MEMORY_GROWTH
  -sINITIAL_MEMORY=268435456
  -sMAXIMUM_MEMORY=1073741824
  -sSTACK_SIZE=1048576
  -sEXIT_RUNTIME=1
  # The JS-facing surface (frame, audio ring, input ring); emscripten 6
  # exports nothing to the Module by default.
  "-sEXPORTED_FUNCTIONS=_main,_fmrb_wasm_frame_seq,_fmrb_wasm_frame_rgba,_fmrb_wasm_frame_width,_fmrb_wasm_frame_height,_fmrb_wasm_audio_ring,_fmrb_wasm_audio_ring_size,_fmrb_wasm_audio_wr,_fmrb_wasm_audio_rate,_fmrb_wasm_audio_volume,_fmrb_wasm_input_ring,_fmrb_wasm_input_ring_events,_fmrb_wasm_input_wr_ptr"
  "-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32,HEAP32"
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
target_link_options(core_web PRIVATE
  ${WASM_CORE_LINK_OPTS}
  -sENVIRONMENT=web,worker
  -sMODULARIZE=1
  -sEXPORT_NAME=createFmrbCore
  --preload-file ${FMRB_ROOT}/flash@/flash
  --profiling-funcs
)
