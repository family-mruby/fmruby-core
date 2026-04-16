idf_component_register(
    SRCS
        "fmrb_gfx.c"
    INCLUDE_DIRS "."
    REQUIRES fmrb_hal fmrb_transport fmrb_common fmrb_msg log
)