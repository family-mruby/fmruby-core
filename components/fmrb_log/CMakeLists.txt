set(COMMON_SRCS
    "fmrb_log.c"
    "fmrb_log_buffer.c"
)

set(PLATFORM_REQUIRES log fmrb_hal fmrb_common)

idf_component_register(
    SRCS ${COMMON_SRCS}
    INCLUDE_DIRS "."
    REQUIRES ${PLATFORM_REQUIRES}
)

