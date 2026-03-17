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

idf_component_register(
    SRCS ${BASIC_CORE_SRCS} ${FMRB_BASIC_SRCS}
    INCLUDE_DIRS "include" "basic"
    REQUIRES fmrb_mem fmrb_common fmrb_msg log
)
