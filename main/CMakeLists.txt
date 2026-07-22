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

# Set up component dependencies based on target
set(COMPONENT_REQUIRES fmrb_hal fmrb_log fmrb_msg fmrb_transport fmrb_toml fmrb_mem
    fmrb_gfx fmrb_audio lua basic
    picoruby-esp32 mem_allocator log tar_lib
    # Remote debugger wire protocol (all targets since Phase 3a).
    msgpack-esp32)

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
    INCLUDE_DIRS "include" "boot" "kernel" "kernel/host" "drivers/led_status" "drivers/usb" "drivers/ble" "drivers/m5gfx" "drivers/i2c_keyboard" "drivers/display_p4" "drivers/touch" "drivers/tab5_keyboard" "drivers/audio_p4" "drivers/wifi" "drivers/remote_desktop" "drivers/debug" "conn_check"
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

if(FMRB_HW_TARGET STREQUAL "ATOM_DISPLAY")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HW_ATOM_DISPLAY)
    message(STATUS "HW target: ATOM_DISPLAY")
endif()

if(IDF_TARGET STREQUAL "esp32p4")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_HW_MODERN)
    message(STATUS "HW target: Modern (ESP32-P4 / Tab5)")
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