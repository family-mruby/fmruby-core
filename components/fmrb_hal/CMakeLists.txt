# Set common sources
set(COMMON_SRCS
    "fmrb_hal.c"
    "fmrb_hal_time.c"
)

# Set platform-specific sources and requirements
if(IDF_TARGET STREQUAL "linux")
    list(APPEND COMMON_SRCS
        "platform/posix/fmrb_hal_time_posix.c"
        "platform/posix/fmrb_hal_gpio_posix.c"
        "platform/posix/fmrb_hal_i2c_posix.c"
        "platform/posix/fmrb_hal_rmt_posix.c"
        "platform/posix/fmrb_hal_pin_manager_posix.c"
        "platform/posix/fmrb_hal_spi_posix.c"
        "platform/posix/fmrb_hal_link_posix.c"
        "platform/posix/fmrb_hal_file_posix.c"
        "platform/posix/fmrb_tmpfs_posix.c"
        "platform/posix/fmrb_hal_uart_posix.c"
    )
    set(PLATFORM_REQUIRES pthread log fmrb_msg fmrb_log fmrb_mem fmrb_cobs fmrb_common)
    set(PLATFORM_PRIV_REQUIRES )
else()
    set(FMRB_HW_TARGET "WROVER" CACHE STRING "Hardware target variant: WROVER, ATOM_DISPLAY, TAB5, NARYAv4")
    list(APPEND COMMON_SRCS
        "platform/esp32/fmrb_hal_time_esp32.c"
        "platform/esp32/fmrb_hal_gpio_esp32.c"
        "platform/esp32/fmrb_hal_i2c_esp32.c"
        "platform/esp32/fmrb_hal_rmt_esp32.c"
        "platform/esp32/fmrb_hal_pin_manager_esp32.c"
        "platform/esp32/fmrb_hal_spi_esp32.c"
        "platform/esp32/fmrb_hal_file_esp32.c"
        "platform/esp32/fmrb_tmpfs_esp32.c"
        "platform/esp32/fmrb_hal_uart_esp32.c"
    )
    set(FMRB_LINK_TRANSPORT "UART" CACHE STRING "Link transport: SPI, UART")
    if(FMRB_HW_TARGET STREQUAL "ATOM_DISPLAY")
        list(APPEND COMMON_SRCS "platform/esp32/fmrb_hal_link_local.c")
        message(STATUS "HW target: ATOM_DISPLAY (local Message Buffer link)")
    elseif(IDF_TARGET STREQUAL "esp32p4")
        # Modern (TAB5 / NARYAv4): monolithic, no WROVER. gfx commands are
        # consumed in-process by the local display task, so use the local link.
        list(APPEND COMMON_SRCS "platform/esp32/fmrb_hal_link_local.c")
        message(STATUS "HW target: Modern P4 (local Message Buffer link)")
    elseif(FMRB_LINK_TRANSPORT STREQUAL "UART")
        list(APPEND COMMON_SRCS "platform/esp32/fmrb_hal_link_uart_esp32.c")
        message(STATUS "HW target: ${FMRB_HW_TARGET} (UART link)")
    else()
        list(APPEND COMMON_SRCS "platform/esp32/fmrb_hal_link_esp32.c")
        message(STATUS "HW target: ${FMRB_HW_TARGET} (SPI link)")
    endif()
    set(PLATFORM_REQUIRES esp_system esp_timer freertos log spi_flash driver fmrb_msg
        fmrb_log fmrb_mem fmrb_cobs fmrb_common esp_littlefs fatfs sdmmc vfs
        esp_driver_i2c esp_driver_rmt esp_driver_sdmmc esp_driver_sdspi)
    set(PLATFORM_PRIV_REQUIRES )
endif()

if(NOT IDF_TARGET STREQUAL "linux")
    set(PRIV_INCLUDE "platform/esp32")
else()
    set(PRIV_INCLUDE "")
endif()

idf_component_register(
    SRCS ${COMMON_SRCS}
    INCLUDE_DIRS "." "platform" "hw_proxy"
    PRIV_INCLUDE_DIRS ${PRIV_INCLUDE}
    REQUIRES ${PLATFORM_REQUIRES}
    PRIV_REQUIRES ${PLATFORM_PRIV_REQUIRES}
)

# FMRB_HW_* board / chip-generation macros: this component compiles
# fmrb_pin_assign.h consumers (pin manager, link header), and the macros are
# PRIVATE per component.
include(${CMAKE_CURRENT_LIST_DIR}/../fmrb_common/fmrb_hw_defines.cmake)
fmrb_add_hw_defines()

# Add Linux-specific compiler definitions
if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_PLATFORM_LINUX=1)
endif()