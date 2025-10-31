set(COMMON_SRCS
    "fmrb_log.c"
)

# Platform-specific requirements
if(IDF_TARGET STREQUAL "linux")
    set(PLATFORM_REQUIRES log)
else()
    set(PLATFORM_REQUIRES log)
endif()

idf_component_register(
    SRCS ${COMMON_SRCS}
    INCLUDE_DIRS "."
    REQUIRES ${PLATFORM_REQUIRES}
)

