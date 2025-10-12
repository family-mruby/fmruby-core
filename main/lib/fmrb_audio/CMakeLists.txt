idf_component_register(
    SRCS
        "fmrb_audio.c"
        "fmrb_audio_mixer.c"
        "fmrb_audio_queue.c"
        "fmrb_audio_stream.c"
    INCLUDE_DIRS "."
    REQUIRES fmrb_hal fmrb_ipc log
)