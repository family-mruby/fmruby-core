# Lua Extensions for FMRuby

This directory contains FMRuby-specific Lua extension libraries.

## Purpose

Extension libraries provide Lua bindings for FMRuby functionality:
- Graphics API (fmrb_gfx)
- Audio API (fmrb_audio)
- Task/Process management
- HID (keyboard/mouse) events
- File system operations

## Structure

Each extension should be implemented as a separate C file:
```
extension/
  ├── fmrb_gfx_lua.c      # Graphics API bindings
  ├── fmrb_audio_lua.c    # Audio API bindings
  ├── fmrb_task_lua.c     # Task management bindings
  └── ...
```

## Implementation Guidelines

1. Use `luaL_Reg` to register module functions
2. Follow Lua C API conventions
3. Integrate with FMRuby's error handling (fmrb_err_t)
4. Use fmrb_malloc for memory allocation
5. Add extension sources to `../CMakeLists.txt` EXTENSION_SRCS

## Example

```c
#include "fmrb_lua.h"
#include "fmrb_gfx.h"

static int lua_gfx_drawPixel(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    uint32_t color = luaL_checkinteger(L, 3);

    fmrb_gfx_draw_pixel(x, y, color);
    return 0;
}

static const luaL_Reg gfx_funcs[] = {
    {"drawPixel", lua_gfx_drawPixel},
    {NULL, NULL}
};

int luaopen_gfx(lua_State *L) {
    luaL_newlib(L, gfx_funcs);
    return 1;
}
```
