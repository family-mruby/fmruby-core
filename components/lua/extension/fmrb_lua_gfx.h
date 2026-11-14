#pragma once

#include "lua.h"
#include "fmrb_app_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register the FmrbGfx and FmrbApp tables in a Lua state.
 *
 * A Lua app runs on the app shell (fmrb_app_shell.h): the shell makes the
 * window, and fmrb_lua_gfx_layout puts its drawer in the global `gfx`.
 * - gfx:fill_rect / draw_rect / fill_round_rect / draw_round_rect /
 *   draw_line / fill_circle / draw_text - drawn from the top-left of the
 *   area the app owns (inside the frame)
 * - gfx:clear(color) - the whole canvas, frame included
 * - gfx:present()
 * - FmrbApp.stop(), FmrbApp.clear_user_area([color]),
 *   FmrbApp.draw_window_frame(), FmrbApp.sleep(ms)
 * - FmrbApp.WINDOW_WIDTH / WINDOW_HEIGHT, user_area_width / user_area_height,
 *   HEADLESS, THEME_*; FmrbGfx colour constants
 */
void fmrb_lua_register_gfx(lua_State* L);

/**
 * The shell's layout reached the app: make or update the global `gfx` (its
 * origin is the area's top-left) and the FmrbApp size fields. A headless app
 * gets gfx = nil.
 */
void fmrb_lua_gfx_layout(lua_State* L, const fmrb_shell_layout_t* layout);

#ifdef __cplusplus
}
#endif
