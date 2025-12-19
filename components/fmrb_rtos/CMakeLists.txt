# RTOS abstraction layer component

# Platform-specific sources and requirements
if(IDF_TARGET STREQUAL "linux")
    set(PLATFORM_REQUIRES pthread log)
else()
    # ESP32: Implementation is header-only (macros wrapping FreeRTOS)
    set(PLATFORM_SRCS)
    set(PLATFORM_REQUIRES freertos log)
endif()

idf_component_register(
    SRCS ${PLATFORM_SRCS}
    INCLUDE_DIRS "." "include"
    REQUIRES ${PLATFORM_REQUIRES}
)
