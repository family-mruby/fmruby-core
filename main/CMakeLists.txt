set(COMPONENTS main)
set(COMPONENT_SRCS
    "main.c"
    "boot/boot.c"
    "app/fmrb_app.c"
    "app/fmrb_app_spawner.c"
    "kernel/fmrb_kernel.c"
    "kernel/host/host_task.c"
    "drivers/fs_proxy/fs_proxy_task.c"
    "drivers/usb/fmrb_keymap.c"
    "drivers/led_status/status_led.c"
)
set(COMPONENT_LINUX_SRCS
    "drivers/usb/usb_task_linux.c"
)
set(COMPONENT_ESP32_SRCS
    "drivers/usb/usb_task.c"
    "drivers/usb/hid_report_parser.c"
    "drivers/usb/hid_device_config.c"
    "drivers/ble/ble_task.c"
    "drivers/hw_proxy/hw_proxy.c"
    "drivers/hw_proxy/hw_proxy_file.c"
    "drivers/hw_proxy/hw_proxy_gpio.c"
    "drivers/hw_proxy/hw_proxy_i2c.c"
    "drivers/hw_proxy/hw_proxy_rmt.c"
    "conn_check/spi_conn_check.c"
    "conn_check/usb_hid_conn_check.c"
    "conn_check/i2c_conn_check.c"
    "conn_check/sd_conn_check.c"
    "drivers/rtc/rx8900.c"
    "drivers/rtc/rtc_task.c"
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
    picoruby-esp32 mem_allocator log tar_lib)

# Add ESP32-specific components
if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND COMPONENT_REQUIRES esp_littlefs esp_psram bt nvs_flash)
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

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS "include" "boot" "kernel" "kernel/host" "drivers/led_status" "drivers/fs_proxy" "drivers/usb" "drivers/ble" "drivers/rtc" "drivers/m5gfx" "drivers/i2c_keyboard" "conn_check"
    REQUIRES ${COMPONENT_REQUIRES}
)

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

# Suppress warnings-as-errors in M5Unified (upstream code we cannot modify)
# Applied unconditionally because managed_components are always compiled
if(TARGET __idf_m5stack__m5unified)
    idf_component_get_property(m5unified_lib m5stack__m5unified COMPONENT_LIB)
    target_compile_options(${m5unified_lib} PRIVATE -Wno-error=maybe-uninitialized)
endif()

# Disable optimization for app.c to enable proper debugging
# Todo: disable it on official release
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-O0 -g3>
)

# Step 2: Generate Ruby bytecode compilation commands (must be after idf_component_register)
generate_ruby_bytecode_commands(KERNEL_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/kernel/mrb)
generate_ruby_bytecode_commands(DEFAULT_APP_RUBY_FILES ${CMAKE_CURRENT_LIST_DIR}/prebuild_scripts/default_app/mrb)