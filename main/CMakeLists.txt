idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES fmrb_hal fmrb_ipc fmrb_gfx fmrb_audio picoruby-esp32 log
)