# Set common sources
set(COMMON_SRCS
    "fmrb_hal.c"
    "fmrb_hal_time.c"
    "fmrb_hal_gpio.c"
)

# Set platform-specific sources and requirements
if(IDF_TARGET STREQUAL "linux")
    list(APPEND COMMON_SRCS
        "platform/linux/fmrb_hal_spi_linux.c"
        "platform/linux/fmrb_hal_ipc_linux.c"
    )
    set(PLATFORM_REQUIRES pthread log fmrb_ipc)
else()
    list(APPEND COMMON_SRCS
        "platform/esp32/fmrb_hal_spi_esp32.c"
        "platform/esp32/fmrb_hal_ipc_esp32.c"
    )
    set(PLATFORM_REQUIRES esp_timer freertos log spi_flash driver pthread fmrb_ipc)
endif()

idf_component_register(
    SRCS ${COMMON_SRCS}
    INCLUDE_DIRS "." "platform"
    REQUIRES ${PLATFORM_REQUIRES}
)

# Add Linux-specific compiler definitions
if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_PLATFORM_LINUX=1)
endif()