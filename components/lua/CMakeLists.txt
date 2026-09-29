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
    "fmrb_lua_hal_stdio.c"
)

# Extension sources
set(EXTENSION_SRCS
    "extension/fmrb_lua_gfx.c"
)

idf_component_register(
    SRCS ${LUA_CORE_SRCS} ${FMRB_LUA_SRCS} ${EXTENSION_SRCS}
    INCLUDE_DIRS "include" "lua" "extension"
    REQUIRES fmrb_mem fmrb_common fmrb_msg fmrb_gfx fmrb_hal log
)

# The Lua core opens, reopens, deletes and renames files through stdio; the
# forced header sends each of those through the file HAL (and its lock)
# instead of the VFS. Core sources only: the submodule is not edited, and
# fmrb_lua_hal_stdio.c must see the C library's own names.
set_source_files_properties(${LUA_CORE_SRCS} PROPERTIES COMPILE_OPTIONS
    "-include;${CMAKE_CURRENT_LIST_DIR}/fmrb_lua_hal_stdio.h")

