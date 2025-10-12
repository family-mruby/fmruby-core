# Set platform-specific requirements
if(IDF_TARGET STREQUAL "linux")
    set(PLATFORM_REQUIRES pthread log)
else()
    set(PLATFORM_REQUIRES esp_timer freertos log spi_flash driver pthread)
endif()

idf_component_register(
    SRCS
        "fmrb_hal.c"
        "fmrb_hal_time.c"
        "fmrb_hal_gpio.c"
        "fmrb_hal_spi.c"
        "fmrb_hal_ipc.c"
    INCLUDE_DIRS "."
    REQUIRES ${PLATFORM_REQUIRES}
)

# Add Linux-specific compiler definitions
if(IDF_TARGET STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FMRB_PLATFORM_LINUX=1)
endif()