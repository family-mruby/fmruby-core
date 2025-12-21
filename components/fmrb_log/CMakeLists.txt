set(COMMON_SRCS
    "fmrb_log.c"
)

set(PLATFORM_REQUIRES log)

idf_component_register(
    SRCS ${COMMON_SRCS}
    INCLUDE_DIRS "."
    REQUIRES ${PLATFORM_REQUIRES}
)

