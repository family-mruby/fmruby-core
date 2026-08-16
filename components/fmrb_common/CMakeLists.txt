# Platform-specific requirements for RTOS abstraction
if(IDF_TARGET STREQUAL "linux")
    set(PLATFORM_REQUIRES pthread log)
else()
    # ESP32: RTOS implementation is header-only (macros wrapping FreeRTOS)
    set(PLATFORM_REQUIRES freertos log)
endif()

idf_component_register(
    SRCS "src/fmrb_task.c" "src/fmrb_debug.c" "src/fmrb_sysinfo.c"
    INCLUDE_DIRS "include"
    REQUIRES picoruby-esp32 ${PLATFORM_REQUIRES}
)
