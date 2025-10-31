set(COMMON_SRCS
    "fmrb_toml.c"
)
set(PLATFORM_REQUIRES fmrb_hal fmrb_log toml_parser)

idf_component_register(
    SRCS ${COMMON_SRCS}
    INCLUDE_DIRS "."
    REQUIRES ${PLATFORM_REQUIRES} 
)
