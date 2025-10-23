# microtar ESP32 Component

idf_component_register(
    SRCS
        "microtar/src/microtar.c"
    INCLUDE_DIRS
        "microtar/src"
    REQUIRES
        log
)
