set(COMMON_SRCS
    "fmrb_msg.c"
)

idf_component_register(
    SRCS ${COMMON_SRCS}
    INCLUDE_DIRS "."
    REQUIRES fmrb_rtos fmrb_hal fmrb_common log
    PRIV_REQUIRES fmrb_log
)
