# Platform-specific requirements for RTOS abstraction
if(IDF_TARGET STREQUAL "linux")
    set(PLATFORM_REQUIRES pthread log)
else()
    # ESP32: RTOS implementation is header-only (macros wrapping FreeRTOS)
    set(PLATFORM_REQUIRES freertos log)
endif()

idf_component_register(
    INCLUDE_DIRS "."
    REQUIRES picoruby-esp32 ${PLATFORM_REQUIRES}
)
