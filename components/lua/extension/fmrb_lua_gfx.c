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
#include "fmrb_boundary_recv.h"
#include <string.h>

static const char *TAG = "lua_gfx";

// Userdata type for graphics context. ox / oy is the drawing origin: the
// top-left of the area the app owns, which the shell works out (the same idea
// as mrb_gfx_data of the mruby binding). Every draw call adds it; clear and
// present do not.
typedef struct {
    fmrb_gfx_context_t ctx;
    fmrb_canvas_handle_t canvas_id;
    int16_t ox, oy;
} lua_gfx_data;

#define GX(d, x) ((int16_t)((d)->ox + (x)))
#define GY(d, y) ((int16_t)((d)->oy + (y)))

// The receiver number of the app boundary's FmrbGfx functions
// (fmrb_boundary_recv.h): the canvas, with this gfx's origin bound for the call.
static int32_t fmrb_lua_recv_gfx(lua_State *L, int idx)
{
    lua_gfx_data *data = (lua_gfx_data *)luaL_checkudata(L, idx, "FmrbGfx");
    if (!data || !data->ctx) {
        luaL_error(L, "Graphics not initialized");
        return -1;
    }
    return fmrb_gfx_recv_bind(data->canvas_id, data->ox, data->oy);
}

// FmrbGfx methods generated from sig/ (tools/boundary/gen.rb): fill_rect
#include "fmrb_gen/fmrb_boundary_lua.inc"

// gfx:drawRect(x, y, w, h, color)
static int lua_gfx_draw_rect(lua_State* L) {
    lua_gfx_data *data = (lua_gfx_data *)luaL_checkudata(L, 1, "FmrbGfx");
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    int w = luaL_checkinteger(L, 4);
    int h = luaL_checkinteger(L, 5);
    int color = luaL_checkinteger(L, 6);

    if (!data || !data->ctx) {
        return luaL_error(L, "Graphics not initialized");
    }

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_rect(&cmd, data->canvas_id, GX(data, x), GY(data, y),
                      (uint16_t)w, (uint16_t)h, (fmrb_color_t)color, false);

    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        return luaL_error(L, "drawRect failed: %d", ret);
    }

    lua_pushvalue(L, 1);  // Return self
    return 1;
}

// gfx:drawString(text, x, y, color [, bg_color])
static int lua_gfx_draw_string(lua_State* L) {
    lua_gfx_data *data = (lua_gfx_data *)luaL_checkudata(L, 1, "FmrbGfx");
    const char *text = luaL_checkstring(L, 2);
    int x = luaL_checkinteger(L, 3);
    int y = luaL_checkinteger(L, 4);
    int color = luaL_checkinteger(L, 5);

    // Optional background color (6th argument)
    int bg_color = 0;
    bool bg_transparent = true;
    if (lua_gettop(L) >= 6) {
        bg_color = luaL_checkinteger(L, 6);
        bg_transparent = false;
    }

    if (!data || !data->ctx) {
        return luaL_error(L, "Graphics not initialized");
    }

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_text(&cmd, data->canvas_id, GX(data, x), GY(data, y), text,
                      (fmrb_color_t)color, (fmrb_color_t)bg_color,
                      bg_transparent, FMRB_FONT_SIZE_MEDIUM, 0);

    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        return luaL_error(L, "drawString failed: %d", ret);
    }

    lua_pushvalue(L, 1);  // Return self
    return 1;
}

// gfx:present()
static int lua_gfx_present(lua_State* L) {
    lua_gfx_data *data = (lua_gfx_data *)luaL_checkudata(L, 1, "FmrbGfx");

    if (!data || !data->ctx) {
        return luaL_error(L, "Graphics not initialized");
    }

    // Get current app context for window position
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return luaL_error(L, "No app context available");
    }

    // Window position comes from the TOML config; 0xFF = no transparency.
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_present(&cmd, data->canvas_id, (int16_t)ctx->window_pos_x,
                         (int16_t)ctx->window_pos_y, 0xFF);

    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        return luaL_error(L, "present failed: %d", ret);
    }

    lua_pushvalue(L, 1);  // Return self
    return 1;
}

// gfx:clear(color)
static int lua_gfx_clear(lua_State* L) {
    lua_gfx_data *data = (lua_gfx_data *)luaL_checkudata(L, 1, "FmrbGfx");
    int color = luaL_checkinteger(L, 2);

    if (!data || !data->ctx) {
        return luaL_error(L, "Graphics not initialized");
    }

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_clear(&cmd, data->canvas_id, (fmrb_color_t)color);

    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        return luaL_error(L, "clear failed: %d", ret);
    }

    lua_pushvalue(L, 1);  // Return self
    return 1;
}

// gfx:draw_line(x1, y1, x2, y2, color)
static int lua_gfx_draw_line(lua_State* L) {
    lua_gfx_data *data = (lua_gfx_data *)luaL_checkudata(L, 1, "FmrbGfx");
    int x1 = luaL_checkinteger(L, 2);
    int y1 = luaL_checkinteger(L, 3);
    int x2 = luaL_checkinteger(L, 4);
    int y2 = luaL_checkinteger(L, 5);
    int color = luaL_checkinteger(L, 6);

    if (!data || !data->ctx) {
        return luaL_error(L, "Graphics not initialized");
    }

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_line(&cmd, data->canvas_id, GX(data, x1), GY(data, y1),
                      GX(data, x2), GY(data, y2), (fmrb_color_t)color);

    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        return luaL_error(L, "draw_line failed: %d", ret);
    }

    lua_pushvalue(L, 1);  // Return self
    return 1;
}

// gfx:fill_round_rect(x, y, w, h, radius, color)
static int lua_gfx_fill_round_rect(lua_State* L) {
    lua_gfx_data *data = (lua_gfx_data *)luaL_checkudata(L, 1, "FmrbGfx");
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    int w = luaL_checkinteger(L, 4);
    int h = luaL_checkinteger(L, 5);
    int r = luaL_checkinteger(L, 6);
    int color = luaL_checkinteger(L, 7);

    if (!data || !data->ctx) {
        return luaL_error(L, "Graphics not initialized");
    }

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_round_rect(&cmd, data->canvas_id, GX(data, x), GY(data, y),
                            (int16_t)w, (int16_t)h, (int16_t)r,
                            (fmrb_color_t)color, true);

    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        return luaL_error(L, "fill_round_rect failed: %d", ret);
    }

    lua_pushvalue(L, 1);  // Return self
    return 1;
}

// gfx:draw_round_rect(x, y, w, h, radius, color)
static int lua_gfx_draw_round_rect(lua_State* L) {
    lua_gfx_data *data = (lua_gfx_data *)luaL_checkudata(L, 1, "FmrbGfx");
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    int w = luaL_checkinteger(L, 4);
    int h = luaL_checkinteger(L, 5);
    int r = luaL_checkinteger(L, 6);
    int color = luaL_checkinteger(L, 7);

    if (!data || !data->ctx) {
        return luaL_error(L, "Graphics not initialized");
    }

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_round_rect(&cmd, data->canvas_id, GX(data, x), GY(data, y),
                            (int16_t)w, (int16_t)h, (int16_t)r,
                            (fmrb_color_t)color, false);

    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        return luaL_error(L, "draw_round_rect failed: %d", ret);
    }

    lua_pushvalue(L, 1);  // Return self
    return 1;
}

// gfx:fill_circle(x, y, radius, color)
static int lua_gfx_fill_circle(lua_State* L) {
    lua_gfx_data *data = (lua_gfx_data *)luaL_checkudata(L, 1, "FmrbGfx");
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    int r = luaL_checkinteger(L, 4);
    int color = luaL_checkinteger(L, 5);

    if (!data || !data->ctx) {
        return luaL_error(L, "Graphics not initialized");
    }

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_circle(&cmd, data->canvas_id, GX(data, x), GY(data, y),
                        (int16_t)r, (fmrb_color_t)color, true);

    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        return luaL_error(L, "fill_circle failed: %d", ret);
    }

    lua_pushvalue(L, 1);  // Return self
    return 1;
}

// Method table
static const luaL_Reg gfx_methods[] = {
    FMRB_GEN_LUA_FmrbGfx_METHODS
    {"draw_rect", lua_gfx_draw_rect},
    {"fill_round_rect", lua_gfx_fill_round_rect},
    {"draw_round_rect", lua_gfx_draw_round_rect},
    {"draw_line", lua_gfx_draw_line},
    {"fill_circle", lua_gfx_fill_circle},
    {"draw_text", lua_gfx_draw_string},
    {"present", lua_gfx_present},
    {"clear", lua_gfx_clear},
    {NULL, NULL}
};

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

// FmrbApp.stop() - end the app after the current function returns
static int lua_app_stop(lua_State* L) {
    fmrb_app_shell_stop(fmrb_current());
    return 0;
}

// FmrbApp.clear_user_area([color]) - fill the area the app owns (the theme's
// page colour by default) and draw the frame again
static int lua_app_clear_user_area(lua_State* L) {
    int color = (int)luaL_optinteger(L, 1, -1);
    fmrb_app_shell_clear_user_area(fmrb_current(), color);
    return 0;
}

// FmrbApp.draw_window_frame() - draw the frame again, after drawing near it
static int lua_app_draw_window_frame(lua_State* L) {
    (void)L;
    fmrb_app_shell_draw_frame(fmrb_current());
    return 0;
}

// FmrbApp table with helper functions
static const luaL_Reg app_functions[] = {
    {"sleep", lua_app_sleep},
    {"stop", lua_app_stop},
    {"clear_user_area", lua_app_clear_user_area},
    {"draw_window_frame", lua_app_draw_window_frame},
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
    lua_pop(L, 1);

    // Create FmrbGfx global table (colour constants; the drawer itself is
    // the global gfx the shell's layout makes)
    lua_newtable(L);

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

    // Add window size global variables (will be set by C code)
    fmrb_app_task_context_t* ctx = fmrb_current();
    if (ctx) {
        lua_pushinteger(L, ctx->window_width);
        lua_setfield(L, -2, "WINDOW_WIDTH");
        lua_pushinteger(L, ctx->window_height);
        lua_setfield(L, -2, "WINDOW_HEIGHT");
        lua_pushinteger(L, ctx->window_width);
        lua_setfield(L, -2, "user_area_width");
        lua_pushinteger(L, ctx->window_height);
        lua_setfield(L, -2, "user_area_height");
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

    FMRB_LOGI(TAG, "FmrbGfx and FmrbApp modules registered to Lua");
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
            luaL_getmetatable(L, "FmrbGfx");
            lua_setmetatable(L, -2);
            lua_setglobal(L, "gfx");
        }
        data->ctx = fmrb_gfx_get_global_context();
        data->canvas_id = (fmrb_canvas_handle_t)layout->canvas;
        data->ox = layout->area_x;
        data->oy = layout->area_y;
    }

    lua_getglobal(L, "FmrbApp");
    if (lua_istable(L, -1)) {
        lua_pushinteger(L, layout->window_w);
        lua_setfield(L, -2, "WINDOW_WIDTH");
        lua_pushinteger(L, layout->window_h);
        lua_setfield(L, -2, "WINDOW_HEIGHT");
        lua_pushinteger(L, layout->area_w);
        lua_setfield(L, -2, "user_area_width");
        lua_pushinteger(L, layout->area_h);
        lua_setfield(L, -2, "user_area_height");
    }
    lua_pop(L, 1);
}
