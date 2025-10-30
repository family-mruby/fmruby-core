idf_component_register(
    SRCS
        "fmrb_gfx.c"
        "fmrb_gfx_commands.c"
    INCLUDE_DIRS "." "../../include"
    REQUIRES fmrb_hal fmrb_link log
)