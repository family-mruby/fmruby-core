idf_component_register(
    SRCS
        "fmrb_audio.c"
    INCLUDE_DIRS "."
    REQUIRES fmrb_hal fmrb_ipc log
)