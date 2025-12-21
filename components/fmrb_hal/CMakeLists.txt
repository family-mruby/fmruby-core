# Set common sources
set(COMMON_SRCS
    "fmrb_hal.c"
    "fmrb_hal_time.c"
    "fmrb_hal_gpio.c"
)

# Set platform-specific sources and requirements
if(IDF_TARGET STREQUAL "linux")
    list(APPEND COMMON_SRCS
        "platform/posix/fmrb_hal_spi_posix.c"
        "platform/posix/fmrb_hal_link_posix.c"
        "platform/posix/fmrb_hal_file_posix.c"
        "platform/posix/fmrb_hal_uart_posix.c"
    )
    set(PLATFORM_REQUIRES pthread log fmrb_msg fmrb_log fmrb_mem fmrb_cobs fmrb_common)
    set(PLATFORM_PRIV_REQUIRES )
else()
    list(APPEND COMMON_SRCS
        "platform/esp32/fmrb_hal_spi_esp32.c"
        "platform/esp32/fmrb_hal_link_esp32.c"
        "platform/esp32/fmrb_hal_file_esp32.c"
        "platform/esp32/fmrb_hal_uart_esp32.c"
    )
    set(PLATFORM_REQUIRES esp_system esp_timer freertos log spi_flash driver fmrb_msg
        fmrb_log fmrb_mem fmrb_cobs fmrb_common esp_littlefs fatfs sdmmc)
    set(PLATFORM_PRIV_REQUIRES )
endif()

idf_component_register(
    SRCS ${COMMON_SRCS}
    INCLUDE_DIRS "." "platform"
    REQUIRES ${PLATFORM_REQUIRES}
    PRIV_REQUIRES ${PLATFORM_PRIV_REQUIRES}
)

# Add Linux-specific compiler definitions
if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_PLATFORM_LINUX=1)
endif()