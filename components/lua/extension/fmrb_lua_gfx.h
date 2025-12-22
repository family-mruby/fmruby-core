#pragma once

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register FmrbGfx module to Lua state
 *
 * Provides graphics drawing functions for Lua applications:
 * - FmrbGfx.new(canvas_id) - Create graphics object
 * - gfx:fillRect(x, y, w, h, color) - Draw filled rectangle
 * - gfx:drawString(text, x, y, color) - Draw text string
 * - gfx:present(x, y) - Present canvas to screen
 * - gfx:clear(color) - Clear canvas with color
 *
 * Color constants (RGB332 format):
 * - FmrbGfx.COLOR_BLACK, COLOR_WHITE, COLOR_RED, COLOR_GREEN,
 *   COLOR_BLUE, COLOR_YELLOW, COLOR_MAGENTA, COLOR_CYAN
 */
void fmrb_lua_register_gfx(lua_State* L);

#ifdef __cplusplus
}
#endif
