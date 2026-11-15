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
 * - gfx:<method> - every FmrbGfx method on the app boundary, generated from
 *   sig/ (doc/app_boundary) with Ruby's names and argument order, drawn from
 *   the top-left of the area the app owns (inside the frame); the methods
 *   with optional arguments (draw_text, present, set_font ...) come from a
 *   small Lua prelude over their *_raw forms
 * - FmrbGfx.rgb_to_332(r, g, b); FmrbGfx colour constants
 * - FmrbApp.stop(), FmrbApp.clear_user_area([color]),
 *   FmrbApp.draw_window_frame(), FmrbApp.sleep(ms)
 * - FmrbApp.user_area_width() / user_area_height() / window_width() /
 *   window_height(), HEADLESS, THEME_*
 */
void fmrb_lua_register_gfx(lua_State* L);

/**
 * The shell's layout reached the app: make or update the global `gfx` (its
 * origin is the area's top-left) and the sizes FmrbApp's functions read. A headless app
 * gets gfx = nil.
 */
void fmrb_lua_gfx_layout(lua_State* L, const fmrb_shell_layout_t* layout);

#ifdef __cplusplus
}
#endif
