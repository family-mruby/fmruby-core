# Lua core library sources (excluding standalone interpreters and tests)
set(LUA_CORE_SRCS
    "lua/lapi.c"
    "lua/lauxlib.c"
    "lua/lbaselib.c"
    "lua/lcode.c"
    "lua/lcorolib.c"
    "lua/lctype.c"
    "lua/ldblib.c"
    "lua/ldebug.c"
    "lua/ldo.c"
    "lua/ldump.c"
    "lua/lfunc.c"
    "lua/lgc.c"
    "lua/linit.c"
    "lua/liolib.c"
    "lua/llex.c"
    "lua/lmathlib.c"
    "lua/lmem.c"
    "lua/loadlib.c"
    "lua/lobject.c"
    "lua/lopcodes.c"
    "lua/loslib.c"
    "lua/lparser.c"
    "lua/lstate.c"
    "lua/lstring.c"
    "lua/lstrlib.c"
    "lua/ltable.c"
    "lua/ltablib.c"
    "lua/ltm.c"
    "lua/lundump.c"
    "lua/lutf8lib.c"
    "lua/lvm.c"
    "lua/lzio.c"
)

# FMRuby Lua wrapper implementation
set(FMRB_LUA_SRCS
    "fmrb_lua.c"
)

# Extension sources
set(EXTENSION_SRCS
    # Add extension sources here
)

idf_component_register(
    SRCS ${LUA_CORE_SRCS} ${FMRB_LUA_SRCS} ${EXTENSION_SRCS}
    INCLUDE_DIRS "include" "lua"
    REQUIRES fmrb_mem fmrb_rtos fmrb_common log
)

# Lua-specific compiler definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    LUA_USE_POSIX
    LUA_USE_DLOPEN
)
