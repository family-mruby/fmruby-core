set(COMPONENTS main)
set(COMPONENT_SRCS
    "main.c"
    "boot/boot.c"
    "app/fmrb_app.c"
    "app/fmrb_app_spawner.c"
    "kernel/fmrb_kernel.c"
    "kernel/host/host_task.c"
    "drivers/usb/fmrb_keymap.c"
    "drivers/led_status/status_led.c"
    # Remote debugger core (target independent). Transports are per-target.
    "drivers/debug/fmrb_debug_proto.c"
    "drivers/debug/fmrb_debug_ctx.c"
    "drivers/debug/fmrb_debugd.c"
)
set(COMPONENT_LINUX_SRCS
    "drivers/usb/usb_task_linux.c"
    "drivers/debug/fmrb_debug_transport_tcp.c"
)
set(COMPONENT_ESP32_SRCS
    "drivers/usb/usb_task.c"
    "drivers/usb/hid_report_parser.c"
    "drivers/usb/hid_device_config.c"
    "drivers/ble/ble_task.c"
    "drivers/ble/ble_framing.c"
    "drivers/debug/fmrb_debug_transport_ble.c"
    "drivers/hw_proxy/hw_proxy.c"
    "drivers/hw_proxy/hw_proxy_file.c"
    "drivers/hw_proxy/hw_proxy_gpio.c"
    "drivers/hw_proxy/hw_proxy_i2c.c"
    "drivers/hw_proxy/hw_proxy_rmt.c"
    "conn_check/spi_conn_check.c"
    "conn_check/usb_hid_conn_check.c"
    "conn_check/i2c_conn_check.c"
    "conn_check/sd_conn_check.c"
)

# Include Ruby to bytecode compilation script
include(${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/compile_ruby_to_bytecode.cmake)

# Find all Ruby files in prebuild_scripts/kernel/
file(GLOB KERNEL_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/kernel/*.rb)

# Find all Ruby files in prebuild_scripts/default_app/
file(GLOB DEFAULT_APP_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/default_app/*.rb)

# Step 1: Prepare Ruby bytecode sources (add C files to COMPONENT_SRCS)
prepare_ruby_bytecode_sources(KERNEL_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/kernel/mrb COMPONENT_SRCS)
prepare_ruby_bytecode_sources(DEFAULT_APP_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/default_app/mrb COMPONENT_SRCS)

# Kernel engine selection (Phase 1 T1-6, Phase 2 T2-5). Default mruby; `spinel`
# swaps the kernel task for the Spinel-compiled combined kernel VM.
set(FMRB_KERNEL_ENGINE "$ENV{FMRB_KERNEL_ENGINE}")
if(NOT FMRB_KERNEL_ENGINE)
    set(FMRB_KERNEL_ENGINE "mruby")
endif()
# Desktop app engine (Phase 4 T4-3). Default mruby; `spinel` swaps system_desktop
# for the Spinel-compiled combined desktop VM (spawned NATIVE, see
# fmrb_app_spawner.c). Independent of the kernel engine so mixed configs
# (mruby kernel + Spinel desktop) are buildable.
set(FMRB_APP_ENGINE_DESKTOP "$ENV{FMRB_APP_ENGINE_DESKTOP}")
if(NOT FMRB_APP_ENGINE_DESKTOP)
    set(FMRB_APP_ENGINE_DESKTOP "mruby")
endif()
set(FMRB_SPINEL_GEN_DIR ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/spinel/gen)

# Compile the Spinel machinery whenever ANY VM is Spinel.
set(FMRB_ANY_SPINEL FALSE)
if(FMRB_KERNEL_ENGINE STREQUAL "spinel" OR FMRB_APP_ENGINE_DESKTOP STREQUAL "spinel")
    set(FMRB_ANY_SPINEL TRUE)
    include(${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/compile_ruby_to_spinel.cmake)
    # sp_net_bin_len (:binstr length) -- referenced by whichever shim(s) below
    # compile. Defined here (not in a per-engine shim) so a mixed
    # mruby-kernel + Spinel-desktop build still resolves it.
    list(APPEND COMPONENT_SRCS "kernel/fmrb_spx_common.c")
endif()

if(FMRB_KERNEL_ENGINE STREQUAL "spinel")
    prepare_ruby_spinel_source(fmrb_kernel_combined ${FMRB_SPINEL_GEN_DIR} COMPONENT_SRCS)
    # The kernel FFI shim (fmrb_msg / transport / status_led) is only needed by
    # the Spinel kernel VM.
    list(APPEND COMPONENT_SRCS "kernel/fmrb_spx_kernel.c")
    message(STATUS "FMRB_KERNEL_ENGINE=spinel: kernel VM replaced by Spinel combined kernel")
endif()

if(FMRB_APP_ENGINE_DESKTOP STREQUAL "spinel")
    prepare_ruby_spinel_source(system_desktop_combined ${FMRB_SPINEL_GEN_DIR} COMPONENT_SRCS)
    # Phase 4 T4-1: FmrbApp / FmrbGfx FFI shims used by the Spinel desktop VM.
    # Plain mrb-free C over the same host_task GFX queue / fmrb_* primitives as
    # the mruby bindings.
    list(APPEND COMPONENT_SRCS "app/fmrb_spx_gfx.c")
    list(APPEND COMPONENT_SRCS "app/fmrb_spx_app.c")
    message(STATUS "FMRB_APP_ENGINE_DESKTOP=spinel: system_desktop replaced by Spinel combined desktop")
endif()

# Set up component dependencies based on target
set(COMPONENT_REQUIRES fmrb_hal fmrb_log fmrb_msg fmrb_transport fmrb_toml fmrb_mem
    fmrb_gfx fmrb_audio lua basic
    picoruby-esp32 mem_allocator log tar_lib
    # Remote debugger wire protocol (all targets since Phase 3a).
    msgpack-esp32)

# Spinel engine: link the Spinel runtime (propagates SP_GC_STACK_MAX + include
# dir INTERFACE).
# Any Spinel VM (kernel and/or desktop) needs fmrb_spinel_rt for the runtime
# archive + its INTERFACE include dir (sp_runtime.h for the generated C,
# fmrb_spinel_host.h for the NATIVE spawn wrappers).
if(FMRB_ANY_SPINEL)
    list(APPEND COMPONENT_REQUIRES fmrb_spinel_rt)
endif()

# Add ESP32-specific components
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND COMPONENT_REQUIRES esp_littlefs esp_psram nvs_flash)
    # ESP32-P4 (Modern/Tab5) has no built-in radio; BLE runs as host-only
    # NimBLE with the controller on the ESP32-C6 coprocessor via esp_hosted
    # (SDIO + vHCI), so ble_task.c builds here too. Also add the local Tab5
    # display task (MIPI-DSI via LovyanGFX) + Tab5 Keyboard + local audio.
    # Modern keeps USB host (added in the else branch below).
    if(IDF_TARGET STREQUAL "esp32p4")
        list(APPEND COMPONENT_SRCS
            "drivers/display_p4/display_p4_task.cpp"
            "drivers/display_p4/display_p4_vm.cpp"
            "drivers/display_p4/display_p4_sprite.cpp"
            "drivers/touch/touch_task.c"
            "drivers/tab5_keyboard/tab5_keyboard.c"
            "drivers/audio_p4/audio_p4_hw.c"
            "drivers/audio_p4/audio_p4_handler.c"
            "drivers/audio_p4/audio_p4_task.c"
            "drivers/display_p4/fonts/misaki/lgfx_misaki.c"
            "drivers/display_p4/fonts/misaki/lgfx_misaki.cpp"
            "kernel/host/host_file_local.c"
            "drivers/wifi/wifi_task.c"
            "drivers/remote_desktop/rd_task.c"
            "drivers/remote_desktop/rd_http.c"
            "drivers/remote_desktop/rd_encoder_jpeg.c"
            "drivers/remote_desktop/rd_encoder_h264.c"
            "drivers/remote_desktop/rd_stream.c"
            "drivers/remote_desktop/rd_input.c"
        )
        # Local audio: APU emulator + ES8388 codec (esp_codec_dev) over I2S
        list(APPEND COMPONENT_REQUIRES m5gfx esp_driver_ppa apu_emu
             esp_codec_dev esp_driver_i2s esp_driver_i2c)
        # BLE: NimBLE host on P4, controller on the C6 via esp_hosted
        list(APPEND COMPONENT_REQUIRES bt esp_hosted)
        # Remote desktop: WiFi STA over esp_hosted (esp_wifi_remote) +
        # HTTP/WS server + JPEG (MJPEG) / H.264 hardware encoders
        list(APPEND COMPONENT_REQUIRES esp_wifi esp_netif esp_event
             esp_http_server esp_driver_jpeg esp_h264)
    else()
        list(APPEND COMPONENT_REQUIRES bt)
    endif()
    if(FMRB_HW_TARGET STREQUAL "ATOM_DISPLAY")
        # ATOM_DISPLAY: no USB host, add M5GFX receiver
        list(REMOVE_ITEM COMPONENT_ESP32_SRCS
            "drivers/usb/usb_task.c"
            "drivers/usb/hid_report_parser.c"
            "drivers/usb/hid_device_config.c"
            "conn_check/usb_hid_conn_check.c"
        )
        list(APPEND COMPONENT_SRCS
            "drivers/m5gfx/m5gfx_task.cpp"
            "drivers/i2c_keyboard/i2c_keyboard.c"
        )
    else()
        list(APPEND COMPONENT_REQUIRES usb_host_hid)
    endif()
    list(APPEND COMPONENT_SRCS ${COMPONENT_ESP32_SRCS})
else()
    list(APPEND COMPONENT_SRCS ${COMPONENT_LINUX_SRCS})
endif()

# Remote desktop viewer page (Modern only): embedded into the binary and
# served by the on-device HTTP server. Sources live in tool/web/remote/.
set(RD_EMBED_FILES "")
if(IDF_TARGET STREQUAL "esp32p4")
    set(RD_EMBED_FILES
        "${CMAKE_CURRENT_LIST_DIR}/../tool/web/remote/index.html"
        "${CMAKE_CURRENT_LIST_DIR}/../tool/web/remote/remote.js"
        "${CMAKE_CURRENT_LIST_DIR}/../tool/web/remote/keymap.js")
endif()

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS "include" "boot" "kernel" "kernel/include" "kernel/host" "drivers/led_status" "drivers/usb" "drivers/ble" "drivers/m5gfx" "drivers/i2c_keyboard" "drivers/display_p4" "drivers/touch" "drivers/tab5_keyboard" "drivers/audio_p4" "drivers/wifi" "drivers/remote_desktop" "drivers/debug" "conn_check"
    REQUIRES ${COMPONENT_REQUIRES}
    EMBED_FILES ${RD_EMBED_FILES}
)

# mruby ABI defines: main/ calls mruby APIs (fmrb_app.c etc.), so it must use
# the exact same layout-affecting MRB_* macros as the rake-built libmruby.
# Must come after idf_component_register: IDF's early requirement scan runs
# this file in script mode (stopping at the register call), where project
# commands like add_compile_definitions do not exist.
include(${CMAKE_CURRENT_LIST_DIR}/../components/picoruby-esp32/mruby_abi_defines.cmake)
fmrb_add_mruby_abi_defines()

# Test mode flags
option(ENABLE_SPI_TEST "Enable SPI connection test" OFF)
option(ENABLE_USB_HID_TEST "Enable USB HID connection test" OFF)
option(ENABLE_I2C_TEST "Enable I2C connection test" OFF)
option(ENABLE_SD_TEST "Enable SD card connection test" OFF)

if(ENABLE_SPI_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ENABLE_SPI_TEST)
    message(STATUS "SPI connection test enabled")
endif()

if(ENABLE_USB_HID_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ENABLE_USB_HID_TEST)
    message(STATUS "USB HID connection test enabled")
endif()

if(ENABLE_I2C_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ENABLE_I2C_TEST)
    message(STATUS "I2C connection test enabled")
endif()

if(ENABLE_SD_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ENABLE_SD_TEST)
    message(STATUS "SD card connection test enabled")
endif()

# FMRB_HW_* board / chip-generation macros (FMRB_HW_MODERN + FMRB_HW_TAB5 /
# FMRB_HW_NARYAV4 / FMRB_HW_ATOM_DISPLAY). Defined via the shared helper so
# every fmrb_pin_assign.h consumer (main, fmrb_hal, picoruby-esp32) selects
# the same board section.
include(${CMAKE_CURRENT_LIST_DIR}/../components/fmrb_common/fmrb_hw_defines.cmake)
fmrb_add_hw_defines()
if(IDF_TARGET STREQUAL "esp32p4")
    if(FMRB_HW_TARGET STREQUAL "NARYAv4")
        message(WARNING "HW target NARYAv4: board is not finalized; building with the Tab5 pin assignment as a placeholder")
    else()
        message(STATUS "HW target: TAB5 (Modern, ESP32-P4 / M5Stack Tab5)")
    endif()
elseif(FMRB_HW_TARGET STREQUAL "ATOM_DISPLAY")
    message(STATUS "HW target: ATOM_DISPLAY")
endif()

# Suppress warnings-as-errors in M5Unified (upstream code we cannot modify)
# Applied unconditionally because managed_components are always compiled
if(TARGET __idf_m5stack__m5unified)
    idf_component_get_property(m5unified_lib m5stack__m5unified COMPONENT_LIB)
    target_compile_options(${m5unified_lib} PRIVATE -Wno-error=maybe-uninitialized)
endif()

# Optimize main component (-O2) while keeping full debug info (-g3) so
# panic backtraces still resolve. Switch to -O0 locally if a particular
# crash needs step debugging.
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-O2 -g3>
)

# Step 2: Generate Ruby bytecode compilation commands (must be after idf_component_register)
generate_ruby_bytecode_commands(KERNEL_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/kernel/mrb)
generate_ruby_bytecode_commands(DEFAULT_APP_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/default_app/mrb)

# Spinel engine: define the switch macro (fmrb_kernel.c) and, when a host
# SPINEL_BIN is available, wire the .rb -> .c regeneration. The docker linux
# build relies on the host-pre-generated .c (rake spinel:gen).
# SP_NO_MMAN on MMU-less targets: the generated Spinel C includes sp_runtime.h,
# which must not pull <sys/mman.h> on ESP32/newlib (mirrors fmrb_spinel_rt's
# SPINEL_PORT_FLAGS). Held as a literal gate here rather than read from that
# component's cache var, because IDF component processing order is not
# guaranteed -- the same reason the MC flags below are duplicated, not
# referenced. Empty on Linux (CONFIG_IDF_TARGET_LINUX), so 64-bit is unchanged.
# SP_STACK_SCRATCH_MAX caps every scratch buffer the runtime puts on the stack.
# Our mruby/Spinel tasks run on 12-16KB FreeRTOS stacks and a single generated
# Ruby method frame already costs 2-4KB there, so the hosted-sized buffers do not
# fit: sp_sprintf's 4KB temp overflowed the system_desktop stack inside vsnprintf
# on the first draw. Below the budget the runtime also drops the stdin/ARGF/File
# line readers and the POSIX path helpers (expand_path/glob/realpath/pwd), so a
# Ruby program that uses one fails to build instead of dying on the device.
set(SPINEL_GEN_PORT_FLAGS "")
if(NOT CONFIG_IDF_TARGET_LINUX)
    set(SPINEL_GEN_PORT_FLAGS ";-DSP_NO_MMAN;-DSP_STACK_SCRATCH_MAX=512")
endif()

# The Spinel codegen emits layout-compatible pointer kind-punning (an IntArray*
# stored in a PolyArray* slot; SymPolyHash*/StrPolyHash* variants; a boxed
# exception temp). gcc <14 warned; gcc 14 promotes -Wincompatible-pointer-types
# and -Wint-conversion to hard errors by default, which the Xtensa toolchain
# (esp-14.x) hits. The generated C is already -w and proven at runtime on Linux,
# so demote these back to warnings for it on every target (harmless where the
# compiler never errored). A future codegen pass emitting explicit casts would
# let this be dropped.
set(SPINEL_GEN_RELAX_FLAGS ";-Wno-error=incompatible-pointer-types;-Wno-error=int-conversion")

if(FMRB_KERNEL_ENGINE STREQUAL "spinel")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_KERNEL_ENGINE_SPINEL)
    # The combined .rb is itself host-generated (gen_kernel_combined.rb) into the
    # gen/ dir by `rake spinel:gen`; this custom command only re-runs when a host
    # SPINEL_BIN is available (native builds). The docker linux build relies on
    # the host-pre-generated .c.
    generate_ruby_spinel_command(
        ${FMRB_SPINEL_GEN_DIR}/fmrb_kernel_combined.rb
        fmrb_kernel_entry ${FMRB_SPINEL_GEN_DIR})
    # The Spinel-generated C pulls in sp_runtime.h, which defines many static
    # helpers a given program leaves unused; suppress warnings (it is generated,
    # not hand-written) so the project's -Werror does not reject it.
    #
    # Multi-instance: this generated program TU -- and ONLY it, among the main
    # component's sources -- is compiled with SP_MULTI_CTX + the allocation
    # override, matching the fmrb_spinel_rt runtime (both-sides flag; a mismatch
    # is a silent ABI break). NOT applied component-wide: host_task.c / fmrb_app.c
    # / the fmrb_spx shims use libc / fmrb_mem and must not be rerouted through an
    # instance pool. Keep the two flags in sync with fmrb_spinel_rt SPINEL_MC_FLAGS.
    set_source_files_properties(${FMRB_SPINEL_GEN_DIR}/fmrb_kernel_combined.c
        PROPERTIES COMPILE_OPTIONS
        "-w;-DSP_MULTI_CTX;-include;${CMAKE_CURRENT_LIST_DIR}/../components/fmrb_spinel_rt/spinel_rt/sp_mem_override.h${SPINEL_GEN_RELAX_FLAGS}${SPINEL_GEN_PORT_FLAGS}")
endif()

# Desktop app engine: same wiring for the Spinel-compiled system_desktop program
# (spawned NATIVE via fmrb_app_spawner.c). Independent of the kernel engine.
if(FMRB_APP_ENGINE_DESKTOP STREQUAL "spinel")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_APP_ENGINE_DESKTOP_SPINEL)
    generate_ruby_spinel_command(
        ${FMRB_SPINEL_GEN_DIR}/system_desktop_combined.rb
        system_desktop_entry ${FMRB_SPINEL_GEN_DIR})
    # Same both-sides MC flags as the kernel combined (a mismatch is a silent ABI
    # break). Only this generated TU gets them, never the fmrb_spx shims.
    set_source_files_properties(${FMRB_SPINEL_GEN_DIR}/system_desktop_combined.c
        PROPERTIES COMPILE_OPTIONS
        "-w;-DSP_MULTI_CTX;-include;${CMAKE_CURRENT_LIST_DIR}/../components/fmrb_spinel_rt/spinel_rt/sp_mem_override.h${SPINEL_GEN_RELAX_FLAGS}${SPINEL_GEN_PORT_FLAGS}")
endif()