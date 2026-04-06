# BASIC interpreter core sources
set(BASIC_CORE_SRCS
    "basic/lexer.c"
    "basic/parser.c"
    "basic/runtime.c"
)

# FMRuby BASIC wrapper implementation
set(FMRB_BASIC_SRCS
    "fmrb_basic.c"
)

# BASIC extensions (graphics console, etc.)
set(BASIC_EXT_SRCS
    "extension/fmrb_basic_gfx.c"
)

idf_component_register(
    SRCS ${BASIC_CORE_SRCS} ${FMRB_BASIC_SRCS} ${BASIC_EXT_SRCS}
    INCLUDE_DIRS "include" "basic" "extension"
    REQUIRES fmrb_mem fmrb_common fmrb_msg fmrb_gfx log
)
