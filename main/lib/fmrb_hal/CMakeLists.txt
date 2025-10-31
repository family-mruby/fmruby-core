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
    set(PLATFORM_REQUIRES pthread log fmrb_msg fmrb_link fmrb_log fmrb_mem )
else()
    list(APPEND COMMON_SRCS
        "platform/esp32/fmrb_hal_spi_esp32.c"
        "platform/esp32/fmrb_hal_link_esp32.c"
        "platform/esp32/fmrb_hal_file_esp32.c"
        "platform/esp32/fmrb_hal_uart_esp32.c"
    )
    set(PLATFORM_REQUIRES esp_system esp_timer freertos log spi_flash driver fmrb_msg 
        fmrb_link fmrb_log fmrb_mem esp_littlefs fatfs sdmmc)
endif()

idf_component_register(
    SRCS ${COMMON_SRCS}
    INCLUDE_DIRS "." "platform" "../../include"
    REQUIRES ${PLATFORM_REQUIRES}
)

# Add Linux-specific compiler definitions
if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_PLATFORM_LINUX=1)
endif()