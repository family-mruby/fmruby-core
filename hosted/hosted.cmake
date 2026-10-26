# hosted/hosted.cmake - what the one-process "hosted" builds share.
#
# The browser build (wasm/) and the native Linux program (native/) are the
# same firmware shape: the Modern machine's core, display task and audio task
# in one process, on the cooperative FreeRTOS port, without ESP-IDF. This file
# is the one list of what that firmware is made of, so the two builds cannot
# drift apart (doc/linux_native/plan.md). What only one of them needs -- its
# output backends, its entry point, its link options -- stays in its own
# CMakeLists.txt.
#
# The including CMakeLists sets, before include():
#
#   FMRB_HOSTED_PLATFORM     wasm | native
#   FMRB_HOSTED_MRUBY_BUILD  the picoruby build name of its libmruby
#                            (family-mruby-wasm / family-mruby-native)
#   FMRB_HOSTED_NATIVE_ISA   ON when the code runs on a real CPU (native):
#                            the Spinel runtime keeps its coroutines (fibers
#                            mmap their stacks), as in the Linux simulation.
#                            OFF for wasm, which takes the MMU-less side
#                            (SP_NO_MMAN) like the ESP32 targets.
#   FMRB_HOSTED_PTR32        ON when pointers are 32-bit (wasm32): MRB_32BIT,
#                            as on the devices. The rake side
#                            (lib/add/family_mruby_<platform>.rb) MUST agree.
#
# and gets:
#
#   freertos_hosted          static library: the vendored kernel, the
#                            cooperative port and its clock for the platform
#   lgfx_hosted              static library: LovyanGFX sprite/font core
#   fmrb_hosted_core_objs(<target>)
#                            an OBJECT library with every shared firmware
#                            source, include path and definition

set(FMRB_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(FMRB_HOSTED_DIR ${CMAKE_CURRENT_LIST_DIR})
set(PICORUBY_DIR ${FMRB_ROOT}/components/picoruby-esp32/picoruby)
set(MRBGEMS ${PICORUBY_DIR}/mrbgems)
set(FMRB_HOSTED_MRUBY_DIR ${PICORUBY_DIR}/build/${FMRB_HOSTED_MRUBY_BUILD})
set(FMRB_HOSTED_LIBMRUBY ${FMRB_HOSTED_MRUBY_DIR}/lib/libmruby.a)

if(NOT FMRB_HOSTED_PLATFORM MATCHES "^(wasm|native)$")
  message(FATAL_ERROR "FMRB_HOSTED_PLATFORM must be wasm or native (got '${FMRB_HOSTED_PLATFORM}')")
endif()

# Engines, the same switch names as the IDF build, but CMake variables only
# (-DFMRB_KERNEL_ENGINE=mruby ...): the environment rake runs in carries the
# device build's choices from .env, which must not change what the browser
# bundle is. The hosted default is the standard configuration: Spinel kernel
# and Spinel editor, mruby desktop. Kernel and editor on mruby is the
# all-mruby configuration the engine check runs second.
foreach(_eng KERNEL_ENGINE:spinel APP_ENGINE_DESKTOP:mruby APP_ENGINE_EDITOR:spinel)
  string(REPLACE ":" ";" _pair ${_eng})
  list(GET _pair 0 _name)
  list(GET _pair 1 _default)
  if(NOT DEFINED FMRB_${_name})
    set(FMRB_${_name} ${_default})
  endif()
  if(NOT FMRB_${_name} MATCHES "^(spinel|mruby)$")
    message(FATAL_ERROR "FMRB_${_name} must be spinel or mruby (got '${FMRB_${_name}}')")
  endif()
endforeach()
message(STATUS "hosted (${FMRB_HOSTED_PLATFORM}): kernel=${FMRB_KERNEL_ENGINE} desktop=${FMRB_APP_ENGINE_DESKTOP} editor=${FMRB_APP_ENGINE_EDITOR}")

# rake clean_all deletes the picoruby build tree, this archive included. Warn
# at configure and fail at link: the FreeRTOS PoC (wasm) has no mruby in it
# and must keep working right after a clean.
if(NOT EXISTS ${FMRB_HOSTED_LIBMRUBY})
  message(WARNING "libmruby not found (${FMRB_HOSTED_LIBMRUBY}); run `rake ${FMRB_HOSTED_PLATFORM}:mruby` before building the core")
endif()

# --------------------------------------------------------------- FreeRTOS

set(FREERTOS_DIR ${FMRB_HOSTED_DIR}/vendor/freertos)
set(FREERTOS_IDF_DIR ${FMRB_HOSTED_DIR}/vendor/freertos_idf)

if(FMRB_HOSTED_PLATFORM STREQUAL "wasm")
  set(_port_clock ${FMRB_HOSTED_DIR}/port/port_clock_wasm.c)
else()
  set(_port_clock ${FMRB_HOSTED_DIR}/port/port_clock_posix.c)
endif()

add_library(freertos_hosted STATIC
  ${FREERTOS_DIR}/tasks.c
  ${FREERTOS_DIR}/queue.c
  ${FREERTOS_DIR}/list.c
  ${FREERTOS_DIR}/stream_buffer.c
  ${FMRB_HOSTED_DIR}/port/port.c
  ${FMRB_HOSTED_DIR}/port/wait_for_event.c
  ${_port_clock}
  ${FMRB_HOSTED_DIR}/stub/heap_hosted.c
)

target_include_directories(freertos_hosted PUBLIC
  ${FMRB_HOSTED_DIR}/port
  ${FMRB_HOSTED_DIR}/stub/include
  ${FREERTOS_DIR}/include
  ${FREERTOS_DIR}/include/freertos
  ${FREERTOS_IDF_DIR}
  ${FREERTOS_IDF_DIR}/include
)

# The vendored kernel is ESP-IDF's, warts and all; do not let its style choices
# drown out warnings from our own code.
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  target_compile_options(freertos_hosted PRIVATE -Wno-c23-extensions -Wno-unused-value)
else()
  target_compile_options(freertos_hosted PRIVATE -Wno-unused-value)
endif()
target_compile_options(freertos_hosted PUBLIC -pthread)
if(FMRB_HOSTED_PLATFORM STREQUAL "native")
  # Task stacks are pthread stacks sized from device numbers (32-bit frames,
  # newlib); a 64-bit glibc build asks for four times as much, and never less
  # than 256 KB. Address space only: untouched pages are not backed.
  target_compile_definitions(freertos_hosted PRIVATE
    portHOSTED_TASK_STACK_SCALE=4
    portHOSTED_MIN_TASK_STACK_BYTES=262144)
endif()

# --- LovyanGFX (sprite/font core only; rake hosted:m5gfx fetches + patches) --

set(M5GFX_SRC ${FMRB_HOSTED_DIR}/vendor/m5gfx/src)
if(NOT EXISTS ${M5GFX_SRC}/M5GFX.h)
  message(FATAL_ERROR "M5GFX not found (${M5GFX_SRC}); run `rake hosted:m5gfx` first")
endif()

add_library(lgfx_hosted STATIC
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
  ${FMRB_HOSTED_DIR}/backend/lgfx_common_hosted.cpp
)
target_include_directories(lgfx_hosted PUBLIC ${M5GFX_SRC})
# The platform branch rake hosted:m5gfx patched in; PUBLIC so every consumer's
# view of the lgfx headers picks the same (hosted) platform, ahead of the
# ESP_PLATFORM define the rest of the core build carries.
target_compile_definitions(lgfx_hosted PUBLIC FMRB_LGFX_HOSTED)
target_compile_options(lgfx_hosted PRIVATE -pthread -w)
# GNU ld keeps what a section references; the font tables lgfx_fonts.cpp lists
# but this build does not compile (and the Spinel runtime's socket and crypto
# calls, sp_net.c / sp_crypto.c not being in the snapshot) are only dropped
# when every function and object has a section of its own. wasm-ld collects
# them without being asked; the IDF build passes the same flags.
if(FMRB_HOSTED_PLATFORM STREQUAL "native")
  set(FMRB_HOSTED_SECTION_FLAGS -ffunction-sections -fdata-sections)
  target_compile_options(lgfx_hosted PRIVATE ${FMRB_HOSTED_SECTION_FLAGS})
else()
  set(FMRB_HOSTED_SECTION_FLAGS "")
endif()

# ------------------------------------------------------------------ core
#
# The firmware, assembled the way doc/wasm/implementation_plan.md P4a says:
# the Linux build's compile_commands.json file set is the baseline, minus the
# posix link (fmrb_hal_link_local instead), minus the linux input/debugd/sim
# pieces (the platform's backends + exclusions), plus the local display task
# and the Modern audio pipeline.

# --- sources: components ---------------------------------------------------

set(FMRB_HOSTED_CORE_SRCS
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
  ${FMRB_ROOT}/components/fmrb_common/platform/hosted/fmrb_log_port_hosted.c
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
  ${FMRB_ROOT}/components/fmrb_hal/platform/posix/fmrb_hal_time_posix.c
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
list(APPEND FMRB_HOSTED_CORE_SRCS
  ${LUA_SRCS}
  ${FMRB_ROOT}/components/lua/fmrb_lua.c
  ${FMRB_ROOT}/components/lua/fmrb_lua_hal_stdio.c
  ${FMRB_ROOT}/components/lua/extension/fmrb_lua_gfx.c)
# The Lua core's file access goes through the file HAL, as in
# components/lua/CMakeLists.txt.
set_source_files_properties(${LUA_SRCS} PROPERTIES COMPILE_OPTIONS
  "-include;${FMRB_ROOT}/components/lua/fmrb_lua_hal_stdio.h")

# basic (C++)
list(APPEND FMRB_HOSTED_CORE_SRCS
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
list(APPEND FMRB_HOSTED_CORE_SRCS
  ${MP_EMBED_SRCS}
  ${FMRB_ROOT}/components/micropython/fmrb_mp.c
  ${FMRB_ROOT}/components/micropython/port/mpport.c
  ${FMRB_ROOT}/components/micropython/modules/fmrb_module.c
  ${FMRB_ROOT}/components/micropython/modules/fmrb_bridge.c)

# Spinel runtime. On a real CPU (native) the whole snapshot, coroutines
# included, exactly as the Linux simulation builds it; on wasm the coroutine
# pair is dropped and SP_NO_MMAN set, as on the ESP32 targets (doc/wasm/ P4a).
file(GLOB SPINEL_RT_SRCS ${FMRB_ROOT}/components/fmrb_spinel_rt/spinel_rt/*.c)
if(NOT FMRB_HOSTED_NATIVE_ISA)
  list(REMOVE_ITEM SPINEL_RT_SRCS
    ${FMRB_ROOT}/components/fmrb_spinel_rt/spinel_rt/sp_fiber.c
    ${FMRB_ROOT}/components/fmrb_spinel_rt/spinel_rt/sp_sched.c)
endif()
list(APPEND FMRB_HOSTED_CORE_SRCS
  ${SPINEL_RT_SRCS}
  ${FMRB_ROOT}/components/fmrb_spinel_rt/fmrb_spinel_host.c)

# picoruby gem ports (the linux set: esp32 port dirs are target independent
# where they matter, the FreeRTOS-dependent ones compile against our port)
foreach(gem fmrb-app fmrb-kernel fmrb-const fmrb-log fmrb-io fmrb-filesystem
        fmrb-msgpack fmrb-bmp332 fmrb-fft fmrb-spinel-hello fmrb-raycast
        fmrb-debug fmrb-editor-core fmrb-midi)
  file(GLOB GEM_PORT_SRCS ${MRBGEMS}/picoruby-${gem}/ports/esp32/*.c)
  list(APPEND FMRB_HOSTED_CORE_SRCS ${GEM_PORT_SRCS})
endforeach()
list(APPEND FMRB_HOSTED_CORE_SRCS
  ${MRBGEMS}/picoruby-fmrb-bmp332/src/fmrb_bmp332.c
  ${MRBGEMS}/picoruby-mruby/lib/mruby/mrbgems/mruby-task/ports/freertos/task_hal.c
  ${MRBGEMS}/picoruby-machine/ports/common/machine.c
  ${MRBGEMS}/picoruby-rng/ports/posix/rng.c)

# apu emulator (Modern's local audio synthesis; output goes to the platform's
# audio backend)
list(APPEND FMRB_HOSTED_CORE_SRCS
  ${FMRB_ROOT}/components/apu_emu/src/apu_if_p4.c
  ${FMRB_ROOT}/components/apu_emu/src/apu_helper.c
  ${FMRB_ROOT}/components/apu_emu/src/nofrendo/nes_apu.c
  ${FMRB_ROOT}/components/apu_emu/src/nes_cpu.c
  ${FMRB_ROOT}/components/apu_emu/src/nsf_player.c
  ${FMRB_ROOT}/components/apu_emu/src/fmsq_player.c)

# --- sources: main ---------------------------------------------------------

list(APPEND FMRB_HOSTED_CORE_SRCS
  ${FMRB_ROOT}/main/main.c
  ${FMRB_ROOT}/main/boot/boot.c
  ${FMRB_ROOT}/main/app/fmrb_app.c
  ${FMRB_ROOT}/main/app/fmrb_app_spawner.c
  ${FMRB_ROOT}/main/kernel/fmrb_kernel.c
  ${FMRB_ROOT}/main/kernel/fmrb_secret_path.c
  ${FMRB_ROOT}/main/kernel/fmrb_rtc.c
  ${FMRB_ROOT}/main/kernel/host/host_task.c
  # Modern's "GA storage is our own filesystem" file receiver; the hosted
  # builds have the same one-process shape (host_task.c guards widened
  # accordingly).
  ${FMRB_ROOT}/main/kernel/host/host_file_local.c
  ${FMRB_ROOT}/main/drivers/usb/fmrb_keymap.c
  ${FMRB_ROOT}/main/drivers/usb/fmrb_kana.c
  ${FMRB_ROOT}/main/drivers/led_status/status_led.c
  ${FMRB_ROOT}/main/drivers/debug/fmrb_debug_proto.c
  ${FMRB_ROOT}/main/drivers/debug/fmrb_debug_ctx.c
  # debugd: the editor's on-device debugger takes the local session through
  # it on every build. The native build also starts it on TCP (boot.c skips
  # fmrb_debugd_init on wasm only, where the TCP transport is dead weight the
  # linker keeps cheaply).
  ${FMRB_ROOT}/main/drivers/debug/fmrb_debugd.c
  ${FMRB_ROOT}/main/drivers/debug/fmrb_debug_transport_tcp.c
  ${FMRB_ROOT}/main/conn_check/fs_bench.c
  # the local display task, with the no-panel board (board/none)
  ${FMRB_ROOT}/main/drivers/display_local/display_local_task.cpp
  ${FMRB_ROOT}/main/drivers/display_local/board/none/board_display_none.cpp
  ${FMRB_ROOT}/main/drivers/display_local/display_local_vm.cpp
  ${FMRB_ROOT}/main/drivers/display_local/display_local_sprite.cpp
  ${FMRB_ROOT}/main/drivers/display_local/display_backend.cpp
  ${FMRB_ROOT}/main/drivers/display_local/display_blend_cpu.c
  ${FMRB_ROOT}/main/drivers/display_local/fonts/misaki/lgfx_misaki.c
  ${FMRB_ROOT}/main/drivers/display_local/fonts/misaki/lgfx_misaki.cpp
  # the Modern audio pipeline over the platform's ring backend
  ${FMRB_ROOT}/main/drivers/audio_p4/audio_p4_task.c
  ${FMRB_ROOT}/main/drivers/audio_p4/audio_p4_handler.c
  ${FMRB_ROOT}/lib/add/picoruby-fmrb-fft/native/fmrb_fft_bench.c
  ${FMRB_ROOT}/lib/add/picoruby-fmrb-fft/native/fmrb_fft_spinel.c
  ${FMRB_ROOT}/lib/add/picoruby-fmrb-spinel-hello/native/spinel_hello_native.c
  ${FMRB_ROOT}/lib/add/picoruby-fmrb-raycast/native/raycast_native.c
  ${FMRB_ROOT}/main/kernel/fmrb_spx_common.c)

# Engine shims, chosen the way main/CMakeLists.txt chooses them.
if(FMRB_KERNEL_ENGINE STREQUAL "spinel")
  list(APPEND FMRB_HOSTED_CORE_SRCS ${FMRB_ROOT}/main/kernel/fmrb_spx_kernel.c)
endif()
if(FMRB_APP_ENGINE_DESKTOP STREQUAL "spinel" OR FMRB_APP_ENGINE_EDITOR STREQUAL "spinel")
  list(APPEND FMRB_HOSTED_CORE_SRCS
    ${FMRB_ROOT}/main/app/fmrb_spx_gfx.c
    ${FMRB_ROOT}/main/app/fmrb_spx_app.c)
endif()
if(FMRB_APP_ENGINE_EDITOR STREQUAL "spinel")
  list(APPEND FMRB_HOSTED_CORE_SRCS ${FMRB_ROOT}/main/app/fmrb_spx_editor.c)
endif()

# The hosted half of the board-less pieces: software MJPEG playback, the
# JPEG-encoder stub, LovyanGFX's clock.
list(APPEND FMRB_HOSTED_CORE_SRCS
  ${FMRB_HOSTED_DIR}/backend/display_video_soft.c
  ${FMRB_HOSTED_DIR}/backend/rd_encoder_stub.c
  ${FMRB_HOSTED_DIR}/backend/input_hosted.c)

# Ruby bytecode (mrbc output, target independent) and the host-generated
# Spinel programs. Both are produced by the existing generation steps (rake
# hosted:mrb, which runs what the IDF build's CMake step runs, and rake
# spinel:gen) and reused as-is; regenerating them is not this build's job.
file(GLOB KERNEL_MRB_SRCS ${FMRB_ROOT}/main/prebuild_scripts/kernel/mrb/*.c)
file(GLOB APP_MRB_SRCS ${FMRB_ROOT}/main/prebuild_scripts/default_app/mrb/*.c)
list(APPEND FMRB_HOSTED_CORE_SRCS ${KERNEL_MRB_SRCS} ${APP_MRB_SRCS})
set(SPINEL_GEN ${FMRB_ROOT}/main/prebuild_scripts/spinel/gen)
set(SPINEL_GEN_SRCS
  ${SPINEL_GEN}/fft_kernel.c
  ${SPINEL_GEN}/spinel_hello_kernel.c
  ${SPINEL_GEN}/raycast_kernel.c)
if(FMRB_KERNEL_ENGINE STREQUAL "spinel")
  list(APPEND SPINEL_GEN_SRCS ${SPINEL_GEN}/fmrb_kernel_combined.c)
endif()
if(FMRB_APP_ENGINE_DESKTOP STREQUAL "spinel")
  list(APPEND SPINEL_GEN_SRCS ${SPINEL_GEN}/system_desktop_combined.c)
endif()
if(FMRB_APP_ENGINE_EDITOR STREQUAL "spinel")
  list(APPEND SPINEL_GEN_SRCS ${SPINEL_GEN}/editor_combined.c)
endif()
list(APPEND FMRB_HOSTED_CORE_SRCS ${SPINEL_GEN_SRCS})

set(FMRB_HOSTED_CORE_INCLUDES
  ${FMRB_HOSTED_DIR}/backend
  ${FMRB_ROOT}/components/fmrb_common/include
  ${FMRB_ROOT}/components/fmrb_hal
  ${FMRB_ROOT}/components/fmrb_hal/platform
  ${FMRB_ROOT}/components/fmrb_hal/platform/posix
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
  ${FMRB_HOSTED_MRUBY_DIR}/include
  ${FMRB_HOSTED_MRUBY_DIR}/mrbgems
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
  ${FMRB_ROOT}/main/drivers/display_local
  ${FMRB_ROOT}/main/drivers/display_local/board
  ${FMRB_ROOT}/main/drivers/display_local/board/none
  ${FMRB_ROOT}/main/drivers/audio_p4
  ${FMRB_ROOT}/main/drivers/remote_desktop
  ${FMRB_ROOT}/components/apu_emu/include
  ${FMRB_ROOT}/components/apu_emu/src
  ${FMRB_ROOT}/components/apu_emu/src/nofrendo
)

set(FMRB_HOSTED_CORE_DEFINES
  # Also a compile definition (not only stub sdkconfig.h): many fmrb sources
  # test CONFIG_IDF_TARGET_LINUX before any header chain reaches sdkconfig.h.
  CONFIG_IDF_TARGET_LINUX=1
  # IDF defines ESP_PLATFORM on every target (linux included); the vendored
  # FreeRTOS.h only pulls freertos/idf_additions.h (xTaskCreate, the TLSP
  # delete-callback API) under it.
  ESP_PLATFORM=1
  # One process, no ESP-IDF, the Modern display task in-process: what the
  # browser and the native program have in common, and where they differ
  # from the 3-process simulation (which shares CONFIG_IDF_TARGET_LINUX).
  FMRB_PLATFORM_HOSTED=1
  # The scheduler is the cooperative port: no tick signal, and a thread that
  # blocks in a system call stops every task (hosted/port/).
  FMRB_RTOS_COOPERATIVE=1
  FMRB_HW_FAMILY_MODERN
  # Concurrent app ceiling. The device targets keep the 9 of fmrb_limits.h
  # because every per-app array is sized by it and their RAM is finite; a
  # browser's or a PC's is not, and a demo people poke at wants to be able to
  # open more than five things at once. MUST match the rake side
  # (lib/add/family_mruby_wasm.rb / family_mruby_native.rb): the mrbgem ports
  # (picoruby-fmrb-app/kernel) size arrays by this too, and a mismatch is the
  # same class of bug as a mismatched MRB_* layout define.
  FMRB_MAX_APPS=32
  # The display side has its own ceilings, and they bind first: one canvas per
  # window (the device's 8 is a PSRAM budget, not a design limit) and the
  # GfxBlock program pool that goes with them. Left at their defaults, the 12th
  # app opened onto "Canvas pool full" and died, which would have made the
  # raised app ceiling a fiction.
  DISPLAY_LOCAL_MAX_CANVAS=34
  DISPLAY_LOCAL_VM_MAX_PROGS=64
  PICORB_PLATFORM_POSIX
  PICORB_VM_MRUBY=1
  # mruby ABI -- keep in step with lib/add/family_mruby_<platform>.rb (the
  # role mruby_abi_defines.cmake plays for the IDF builds; the boot-time
  # guard in fmrb_app_init compares sizeof(mrb_state) across the boundary).
  MRB_INT64=1
  MRB_NO_BOXING
  MRB_UTF8_STRING
  MRB_USE_TASK_SCHEDULER
  MRB_TICK_UNIT=5
  MRB_TIMESLICE_TICK_COUNT=10
  MRB_USE_DEBUG_HOOK
  MRB_BASELINE_PROFILE=1
  MRB_TASK_TICK_SELF_SUPPLY
  # Spinel runtime sizing, both sides (runtime TUs and generated TUs)
  SP_GC_STACK_MAX=8192
  SP_DYN_SYMS_MAX=256
  SP_EXC_STACK_MAX=16
  SP_CATCH_STACK_MAX=16
)
# (MicroPython's NLR is setjmp on every target already: its mpconfigport.h.)
if(FMRB_HOSTED_PTR32)
  list(APPEND FMRB_HOSTED_CORE_DEFINES MRB_32BIT)
endif()
# The GC mark work list counts pointers; the IDF build halves it on 64-bit so
# the block stays 32 KB (components/fmrb_spinel_rt/CMakeLists.txt).
if(FMRB_HOSTED_PTR32)
  list(APPEND FMRB_HOSTED_CORE_DEFINES SP_GC_MARK_STACK_MAX=8192)
else()
  list(APPEND FMRB_HOSTED_CORE_DEFINES SP_GC_MARK_STACK_MAX=4096)
endif()
if(FMRB_KERNEL_ENGINE STREQUAL "spinel")
  list(APPEND FMRB_HOSTED_CORE_DEFINES FMRB_KERNEL_ENGINE_SPINEL)
endif()
if(FMRB_APP_ENGINE_DESKTOP STREQUAL "spinel")
  list(APPEND FMRB_HOSTED_CORE_DEFINES FMRB_APP_ENGINE_DESKTOP_SPINEL)
endif()
if(FMRB_APP_ENGINE_EDITOR STREQUAL "spinel")
  list(APPEND FMRB_HOSTED_CORE_DEFINES FMRB_APP_ENGINE_EDITOR_SPINEL)
endif()
list(APPEND FMRB_HOSTED_CORE_DEFINES FMRB_FFT_SPINEL)

# Spinel: runtime TUs and the generated program TUs both build with the
# multi-instance allocation override, and -- off a real CPU -- the MMU-less
# port flags, exactly as fmrb_spinel_rt/CMakeLists.txt + main/CMakeLists.txt
# do for the IDF targets.
set(FMRB_HOSTED_SPINEL_TU_FLAGS
  "-w;-DSP_MULTI_CTX;-include;${FMRB_ROOT}/components/fmrb_spinel_rt/fmrb_sp_tu_bss.h;-include;${FMRB_ROOT}/components/fmrb_spinel_rt/spinel_rt/sp_mem_override.h")
if(NOT FMRB_HOSTED_NATIVE_ISA)
  string(APPEND FMRB_HOSTED_SPINEL_TU_FLAGS ";-DSP_NO_MMAN;-DSP_STACK_SCRATCH_MAX=512")
endif()
set_source_files_properties(${SPINEL_RT_SRCS}
  ${FMRB_ROOT}/components/fmrb_spinel_rt/fmrb_spinel_host.c
  ${SPINEL_GEN_SRCS}
  PROPERTIES COMPILE_OPTIONS "${FMRB_HOSTED_SPINEL_TU_FLAGS}")
# The gems' receivers include their program's generated header, so they share
# the program's flags (SP_MULTI_CTX is an ABI flag) and see gen/.
set_source_files_properties(
  ${FMRB_ROOT}/lib/add/picoruby-fmrb-fft/native/fmrb_fft_spinel.c
  ${FMRB_ROOT}/lib/add/picoruby-fmrb-spinel-hello/native/spinel_hello_native.c
  ${FMRB_ROOT}/lib/add/picoruby-fmrb-raycast/native/raycast_native.c
  PROPERTIES COMPILE_OPTIONS "${FMRB_HOSTED_SPINEL_TU_FLAGS};-I${SPINEL_GEN}")

# The shared OBJECT library. The platform adds its own backends with
# target_sources and its own definitions with target_compile_definitions.
function(fmrb_hosted_core_objs target)
  add_library(${target} OBJECT ${FMRB_HOSTED_CORE_SRCS})
  target_link_libraries(${target} PUBLIC lgfx_hosted freertos_hosted)
  target_include_directories(${target} PRIVATE ${FMRB_HOSTED_CORE_INCLUDES})
  target_compile_definitions(${target} PRIVATE ${FMRB_HOSTED_CORE_DEFINES})
  target_compile_options(${target} PRIVATE -pthread -Wno-format ${FMRB_HOSTED_SECTION_FLAGS})
endfunction()
