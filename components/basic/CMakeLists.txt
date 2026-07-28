# Family BASIC compatible interpreter core (C++). Host independent: no IDF /
# fmruby headers, so the same sources build with a plain host g++ for the
# golden tests (see test/, `rake basic:test`).
set(BASIC_CORE_SRCS
    "core/basic_core.cpp"
    "core/basic_charset.cpp"
    "core/basic_tokenizer.cpp"
    "core/basic_vars.cpp"
    "core/basic_expr.cpp"
    "core/basic_exec.cpp"
    "core/basic_screen.cpp"
    "core/basic_sprite.cpp"
)

# Adapter between the app task and the core (memory pool, console, exit signal)
set(FMRB_BASIC_SRCS
    "fmrb_basic.cpp"
)

# BASIC extensions (graphics console, etc.)
set(BASIC_EXT_SRCS
    "extension/fmrb_basic_gfx.c"
    "assets/basic_font8.c"
    "assets/basic_tile_a.c"
)

idf_component_register(
    SRCS ${BASIC_CORE_SRCS} ${FMRB_BASIC_SRCS} ${BASIC_EXT_SRCS}
    INCLUDE_DIRS "include" "core" "extension" "assets"
    REQUIRES fmrb_mem fmrb_common fmrb_msg fmrb_gfx log
)
