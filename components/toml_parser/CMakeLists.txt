# tomlc99 ESP32 Component

idf_component_register(
    SRCS
        "tomlc99/toml.c"
    INCLUDE_DIRS
        "tomlc99"
    REQUIRES
        log
)

# Add compile options to suppress warnings
# tomlc99 may have unused static functions
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-char-subscripts
)
