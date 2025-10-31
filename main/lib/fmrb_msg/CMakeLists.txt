set(COMMON_SRCS
    "fmrb_msg.c"
)

# Platform-specific requirements
if(IDF_TARGET STREQUAL "linux")
    set(PLATFORM_REQUIRES pthread log)
else()
    set(PLATFORM_REQUIRES esp_system freertos log)
endif()

idf_component_register(
    SRCS ${COMMON_SRCS}
    INCLUDE_DIRS "." "../../include" "../fmrb_hal"
    REQUIRES ${PLATFORM_REQUIRES}
)
