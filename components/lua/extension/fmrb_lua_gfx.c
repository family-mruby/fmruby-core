#include "lua.h"
#include "lauxlib.h"
#include "fmrb_app.h"
#include "fmrb_gfx.h"
#include "fmrb_gfx_cmd.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include "fmrb_theme.h"
#include "fmrb_app_shell.h"
#include "fmrb_gfx_drawer.h"
#include <string.h>

static const char *TAG = "lua_gfx";

// Userdata type for graphics context. What it draws on is a drawer of the app
// (fmrb_gfx_drawer.h): the canvas, and the drawing origin -- the top-left of
// the area the app owns, which the shell works out. The firmware adds the
// origin to every draw call (clear and present take none), as for every
// engine's FmrbGfx (doc/app_boundary W4b).
typedef struct {
    fmrb_gfx_context_t ctx;
    int32_t drawer;
} lua_gfx_data;


// The receiver number of the app boundary's FmrbGfx functions: the drawer.
static int32_t fmrb_lua_recv_gfx(lua_State *L, int idx)
{
    lua_gfx_data *data = (lua_gfx_data *)luaL_checkudata(L, idx, "FmrbGfx");
    if (!data || !data->ctx || data->drawer < 0) {
        luaL_error(L, "Graphics not initialized");
        return -1;
    }
    return data->drawer;
}

// The receiver number of FmrbAudio's: an FmrbAudio.new() table. Nothing
// reads the number yet (the sound is the app's), so every object is 0.
static int32_t fmrb_lua_recv_audio(lua_State *L, int idx)
{
    luaL_checktype(L, idx, LUA_TTABLE);
    return 0;
}

// FmrbGfx methods generated from sig/ (tools/boundary/gen.rb): every method
// of the class on the app boundary, with Ruby's names and argument order.
#include "fmrb_gen/fmrb_boundary_lua.inc"

static const luaL_Reg gfx_methods[] = {
    FMRB_GEN_LUA_FmrbGfx_METHODS
    {NULL, NULL}
};

static const luaL_Reg gfx_functions[] = {
    FMRB_GEN_LUA_FmrbGfx_FUNCTIONS
    {NULL, NULL}
};

// FmrbAudio (generated): called on an object as in Ruby: audio:note_off(0).
static const luaL_Reg audio_methods[] = {
    FMRB_GEN_LUA_FmrbAudio_METHODS
    {NULL, NULL}
};

// Log (generated): Log.info_raw(msg) ...; the prelude adds Log.info.
static const luaL_Reg log_functions[] = {
    FMRB_GEN_LUA_Log_FUNCTIONS
    {NULL, NULL}
};

// The small Lua prelude: the wrappers Ruby has in mrblib (fmrb-gfx.rb) over
// the *_raw methods, so arguments can be left out the same way. Run once with
// the FmrbGfx metatable as its argument.
static const char gfx_prelude[] =
    "local G = ...\n"
    "function G:draw_text(x, y, text, color, bg_color, mixed)\n"
    "  return self:draw_text_raw(x, y, text, color, bg_color or -1, mixed or false)\n"
    "end\n"
    "function G:present(x, y)\n"
    "  if x and y then return self:present_raw(x, y, true) end\n"
    "  return self:present_raw(0, 0, false)\n"
    "end\n"
    "function G:set_font(family, size)\n"
    "  if family == 'default' then return self:set_font_raw('default', 12) end\n"
    "  return self:set_font_raw(family, size or 8)\n"
    "end\n"
    "function G:set_text_size(size) return self:set_text_size_raw(size) end\n"
    "function G:draw_image(id, x, y, scale_x, scale_y)\n"
    "  return self:draw_image_raw(id, x or 0, y or 0, scale_x or 1.0, scale_y or 0.0)\n"
    "end\n"
    "function G:draw_tile(id, src_x, src_y, w, h, dst_x, dst_y)\n"
    "  return self:draw_tile_raw(id, src_x, src_y, w, h, dst_x or 0, dst_y or 0)\n"
    "end\n"
    "function G:draw_image_masked(id, mask_id, x, y)\n"
    "  return self:draw_image_masked_raw(id, mask_id, x or 0, y or 0)\n"
    "end\n"
    "function G:sync_file(path, dest) return self:sync_file_raw(path, dest or path) end\n"
    "function G:transfer_file(path, dest) return self:transfer_file_raw(path, dest or path) end\n"
    "function G:set_composite_regions(regions)\n"
    "  local list = {}\n"
    "  for i, r in ipairs(regions or {}) do\n"
    "    local dx, dy = r.dst_x or 0, r.dst_y or 0\n"
    "    list[i] = { src_x = r.src_x or dx, src_y = r.src_y or dy, dst_x = dx, dst_y = dy,\n"
    "                w = r.w or 0, h = r.h or 0, transparent = r.transparent and true or false }\n"
    "  end\n"
    "  return self:set_composite_regions_raw(list)\n"
    "end\n";

// The wrappers of FmrbAudio, Log, FmrbApp and FmrbFile over their generated *_raw
// functions, with Ruby's defaults (mrblib/fmrb-audio.rb, fmrb-app.rb). Run
// once after the globals are set.
static const char app_prelude[] =
    "local A = FmrbAudio.__methods\n"
    "function FmrbAudio.new() return setmetatable({}, A) end\n"
    "function A:play(path, track) return self:play_raw(path, track or 0) end\n"
    "function A:play_slot(slot_id, instance) return self:play_slot_raw(slot_id, instance or 0) end\n"
    "function A:note_on(channel, freq, volume, duty, sweep)\n"
    "  return self:note_on_raw(channel, freq, volume or 10, duty or 2, sweep or 0)\n"
    "end\n"
    "FmrbAudio.__methods = nil\n"
    "function Log.debug(msg) return Log.debug_raw(tostring(msg)) end\n"
    "function Log.info(msg) return Log.info_raw(tostring(msg)) end\n"
    "function Log.warn(msg) return Log.warn_raw(tostring(msg)) end\n"
    "function Log.error(msg) return Log.error_raw(tostring(msg)) end\n"
    "function FmrbApp.clipboard(limit) return FmrbApp.clipboard_raw(limit or 0) end\n"
    "function FmrbApp.clear_user_area(color) return FmrbApp.clear_user_area_raw(color or -1) end\n"
    "function FmrbApp.request_file_select(mode) return FmrbApp.request_file_select_raw(mode or 'open') end\n"
    "function FmrbApp.request_run(path, prev_pid) return FmrbApp.request_run_raw(path, prev_pid or -1) end\n"
    "function FmrbApp.notify(summary, body) return FmrbApp.notify_raw(summary, body or '') end\n"
    "function FmrbFile.list(dir)\n"
    "  local s = FmrbFile.list_raw(dir)\n"
    "  if s == nil then return nil end\n"
    "  local t = {}\n"
    "  for name in s:gmatch('([^\\n]*)\\n') do t[#t + 1] = name end\n"
    "  return t\n"
    "end\n";

// FmrbApp.sleep(ms) - Sleep for specified milliseconds
static int lua_app_sleep(lua_State* L) {
    int ms = luaL_checkinteger(L, 1);

    if (ms < 0) {
        return luaL_error(L, "Sleep time must be non-negative");
    }

    // Convert milliseconds to ticks (assuming 1ms per tick for FreeRTOS)
    fmrb_task_delay_ms(ms);

    return 0;
}

// FmrbApp: sleep is Lua's own; the rest is the app boundary's (generated):
// the window's sizes and state as functions (FmrbApp.user_area_width()),
// stop, draw_window_frame, the requests, the machine's state.
// FmrbApp table with helper functions
static const luaL_Reg app_functions[] = {
    {"sleep", lua_app_sleep},
    FMRB_GEN_LUA_FmrbApp_FUNCTIONS
    {NULL, NULL}
};

// FmrbFile (generated): FmrbFile.read(path) ...; the prelude adds list.
static const luaL_Reg file_functions[] = {
    FMRB_GEN_LUA_FmrbFile_FUNCTIONS
    {NULL, NULL}
};

/**
 * Register FmrbGfx module to Lua state
 */
void fmrb_lua_register_gfx(lua_State* L) {
    // Create metatable for FmrbGfx userdata
    luaL_newmetatable(L, "FmrbGfx");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, gfx_methods, 0);
    if (luaL_loadbufferx(L, gfx_prelude, sizeof(gfx_prelude) - 1, "=fmrb_gfx", "t") != LUA_OK) {
        FMRB_LOGE(TAG, "gfx prelude: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    } else {
        lua_pushvalue(L, -2);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            FMRB_LOGE(TAG, "gfx prelude: %s", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    // Create FmrbGfx global table (colour constants and the class's own
    // functions; the drawer itself is the global gfx the shell's layout makes)
    lua_newtable(L);
    luaL_setfuncs(L, gfx_functions, 0);

    // Add color constants (matching mruby API)
    lua_pushinteger(L, 0x00);  // Black
    lua_setfield(L, -2, "BLACK");
    lua_pushinteger(L, 0xFF);  // White
    lua_setfield(L, -2, "WHITE");
    lua_pushinteger(L, 0xE0);  // Red
    lua_setfield(L, -2, "RED");
    lua_pushinteger(L, 0x1C);  // Green
    lua_setfield(L, -2, "GREEN");
    lua_pushinteger(L, 0x03);  // Blue
    lua_setfield(L, -2, "BLUE");
    lua_pushinteger(L, 0xFC);  // Yellow
    lua_setfield(L, -2, "YELLOW");
    lua_pushinteger(L, 0xE3);  // Magenta
    lua_setfield(L, -2, "MAGENTA");
    lua_pushinteger(L, 0x1F);  // Cyan
    lua_setfield(L, -2, "CYAN");

    lua_setglobal(L, "FmrbGfx");

    // Create FmrbApp global table
    lua_newtable(L);
    luaL_setfuncs(L, app_functions, 0);

    fmrb_app_task_context_t* ctx = fmrb_current();
    if (ctx) {
        lua_pushboolean(L, ctx->headless);
        lua_setfield(L, -2, "HEADLESS");
    }

    /* The system theme, so a Lua app draws its window frame in the colours
       every other window uses. Without these it can only spell the light
       theme out (0xC5, 0x60) and a changed theme leaves it behind. */
    const fmrb_theme_t *theme = fmrb_theme_get();
    lua_pushinteger(L, theme->desktop_bg);
    lua_setfield(L, -2, "THEME_DESKTOP_BG");
    lua_pushinteger(L, theme->menu_bg);
    lua_setfield(L, -2, "THEME_MENU_BG");
    lua_pushinteger(L, theme->window_bg);
    lua_setfield(L, -2, "THEME_WINDOW_BG");
    lua_pushinteger(L, theme->text);
    lua_setfield(L, -2, "THEME_TEXT");
    lua_pushinteger(L, theme->text_light);
    lua_setfield(L, -2, "THEME_TEXT_LIGHT");
    lua_pushinteger(L, theme->highlight);
    lua_setfield(L, -2, "THEME_HIGHLIGHT");
    lua_pushinteger(L, theme->border);
    lua_setfield(L, -2, "THEME_BORDER");
    lua_pushinteger(L, theme->button);
    lua_setfield(L, -2, "THEME_BUTTON");
    lua_pushinteger(L, theme->dir_color);
    lua_setfield(L, -2, "THEME_DIR_COLOR");

    lua_setglobal(L, "FmrbApp");

    // FmrbAudio: the class table (new and the constants), its objects'
    // metatable handed to the prelude as FmrbAudio.__methods.
    lua_newtable(L);
    static const struct { const char *name; int value; } audio_consts[] = {
        {"MAIN", 0}, {"SUB", 1},
        {"CH_PULSE1", 0}, {"CH_PULSE2", 1}, {"CH_TRIANGLE", 2}, {"CH_NOISE", 3},
        {"NSF_IDLE", 0}, {"NSF_PLAYING", 1}, {"NSF_FADING", 2}, {"NSF_ENDED", 3},
        {"NSF_END_SILENCE", 1}, {"NSF_END_LOOP", 2}, {"NSF_END_LIMIT", 3},
    };
    for (size_t i = 0; i < sizeof(audio_consts) / sizeof(audio_consts[0]); i++) {
        lua_pushinteger(L, audio_consts[i].value);
        lua_setfield(L, -2, audio_consts[i].name);
    }
    luaL_newmetatable(L, "FmrbAudio");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, audio_methods, 0);
    lua_setfield(L, -2, "__methods");
    lua_setglobal(L, "FmrbAudio");

    lua_newtable(L);
    luaL_setfuncs(L, log_functions, 0);
    lua_setglobal(L, "Log");

    lua_newtable(L);
    luaL_setfuncs(L, file_functions, 0);
    lua_setglobal(L, "FmrbFile");

    if (luaL_loadbufferx(L, app_prelude, sizeof(app_prelude) - 1, "=fmrb_app", "t") != LUA_OK ||
        lua_pcall(L, 0, 0, 0) != LUA_OK) {
        FMRB_LOGE(TAG, "app prelude: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    FMRB_LOGI(TAG, "FmrbGfx, FmrbApp, FmrbAudio, FmrbFile and Log registered to Lua");
}

void fmrb_lua_gfx_layout(lua_State* L, const fmrb_shell_layout_t* layout)
{
    if (layout->canvas < 0) {
        lua_pushnil(L);
        lua_setglobal(L, "gfx");
    } else {
        lua_getglobal(L, "gfx");
        lua_gfx_data *data = (lua_gfx_data *)luaL_testudata(L, -1, "FmrbGfx");
        lua_pop(L, 1);
        if (!data) {
            data = (lua_gfx_data *)lua_newuserdatauv(L, sizeof(lua_gfx_data), 0);
            memset(data, 0, sizeof(*data));
            data->drawer = -1;
            luaL_getmetatable(L, "FmrbGfx");
            lua_setmetatable(L, -2);
            lua_setglobal(L, "gfx");
        }
        // One drawer for the app's gfx, kept for the app's life (the app's
        // drawers go with it); the layout moves its canvas and origin.
        if (data->drawer < 0) {
            data->drawer = fmrb_gfx_drawer_open(layout->canvas, layout->area_w, layout->area_h);
        }
        fmrb_gfx_drawer_set_canvas(data->drawer, layout->canvas);
        fmrb_gfx_drawer_set_origin(data->drawer, layout->area_x, layout->area_y,
                                   layout->area_w, layout->area_h);
        data->ctx = data->drawer >= 0 ? fmrb_gfx_get_global_context() : NULL;
    }
}
