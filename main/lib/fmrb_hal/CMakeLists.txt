# Set platform-specific requirements
if(CONFIG_IDF_TARGET_LINUX)
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