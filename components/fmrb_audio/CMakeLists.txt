idf_component_register(
    SRCS
        "fmrb_audio.c"
        "fmrb_wav.c"
    INCLUDE_DIRS "."
    REQUIRES fmrb_transport fmrb_common log
)