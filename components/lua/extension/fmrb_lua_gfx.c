#include "lua.h"
#include "lauxlib.h"
#include "fmrb_app.h"
#include "fmrb_gfx.h"
#include "fmrb_app_canvas.h"
#include "fmrb_gfx_cmd.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include "fmrb_theme.h"
#include <string.h>

static const char *TAG = "lua_gfx";

// Userdata type for graphics context
typedef struct {
    fmrb_gfx_context_t ctx;
    fmrb_canvas_handle_t canvas_id;
} lua_gfx_data;

// Create new graphics object: gfx = FmrbGfx.new(canvas_id)
static int lua_gfx_new(lua_State* L) {
    int canvas_id = luaL_checkinteger(L, 1);

    FMRB_LOGI(TAG, "FmrbGfx.new called: canvas_id=%d", canvas_id);

    // Create userdata
    lua_gfx_data *data = (lua_gfx_data *)lua_newuserdata(L, sizeof(lua_gfx_data));
    memset(data, 0, sizeof(lua_gfx_data));

    // Get global graphics context
    data->ctx = fmrb_gfx_get_global_context();
    if (!data->ctx) {
        FMRB_LOGE(TAG, "Global graphics context not initialized");
        return luaL_error(L, "Graphics context not initialized");
    }

    data->canvas_id = (fmrb_canvas_handle_t)canvas_id;

    FMRB_LOGI(TAG, "FmrbGfx initialized: canvas_id=%d, ctx=%p",
              (int)data->canvas_id, data->ctx);

    // Set metatable
    luaL_getmetatable(L, "FmrbGfx");
    lua_setmetatable(L, -2);

    return 1;
}

// gfx:fillRect(x, y, w, h, color)
static int lua_gfx_fill_rect(lua_State* L) {
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
    fmrb_gfx_cmd_rect(&cmd, data->canvas_id, (int16_t)x, (int16_t)y,
                      (uint16_t)w, (uint16_t)h, (fmrb_color_t)color, true);

    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        return luaL_error(L, "fillRect failed: %d", ret);
    }

    lua_pushvalue(L, 1);  // Return self
    return 1;
}

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
    fmrb_gfx_cmd_rect(&cmd, data->canvas_id, (int16_t)x, (int16_t)y,
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
    fmrb_gfx_cmd_text(&cmd, data->canvas_id, (int16_t)x, (int16_t)y, text,
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
    fmrb_gfx_cmd_line(&cmd, data->canvas_id, (int16_t)x1, (int16_t)y1,
                      (int16_t)x2, (int16_t)y2, (fmrb_color_t)color);

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
    fmrb_gfx_cmd_round_rect(&cmd, data->canvas_id, (int16_t)x, (int16_t)y,
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
    fmrb_gfx_cmd_round_rect(&cmd, data->canvas_id, (int16_t)x, (int16_t)y,
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
    fmrb_gfx_cmd_circle(&cmd, data->canvas_id, (int16_t)x, (int16_t)y,
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
    {"fill_rect", lua_gfx_fill_rect},
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

// FmrbApp.createCanvas(width, height) - Create canvas and return canvas_id
static int lua_app_create_canvas(lua_State* L) {
    int width = luaL_checkinteger(L, 1);
    int height = luaL_checkinteger(L, 2);

    fmrb_app_task_context_t* ctx = fmrb_current();
    if (!ctx) {
        return luaL_error(L, "No app context available");
    }

    if (ctx->headless) {
        return luaL_error(L, "Cannot create canvas for headless app");
    }

    // The script picks the size here, so this is create_main rather than
    // fmrb_app_canvas_init; registration on the context is the same.
    fmrb_canvas_handle_t canvas_id = FMRB_CANVAS_SCREEN;
    fmrb_err_t ret = fmrb_app_canvas_create_main(
        ctx, (uint16_t)width, (uint16_t)height, ctx->z_order, false, 0,
        &canvas_id);
    if (ret != FMRB_OK) {
        return luaL_error(L, "Failed to create canvas: %d", ret);
    }

    lua_pushinteger(L, canvas_id);
    return 1;
}

// FmrbGfx table with constructor
static const luaL_Reg gfx_functions[] = {
    {"new", lua_gfx_new},
    {NULL, NULL}
};

// FmrbApp table with helper functions
static const luaL_Reg app_functions[] = {
    {"create_canvas", lua_app_create_canvas},
    {"sleep", lua_app_sleep},
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

    // Create FmrbGfx global table
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

    // Add window size global variables (will be set by C code)
    fmrb_app_task_context_t* ctx = fmrb_current();
    if (ctx) {
        lua_pushinteger(L, ctx->window_width);
        lua_setfield(L, -2, "WINDOW_WIDTH");
        lua_pushinteger(L, ctx->window_height);
        lua_setfield(L, -2, "WINDOW_HEIGHT");
        lua_pushboolean(L, ctx->headless);
        lua_setfield(L, -2, "HEADLESS");
    }

    /* The system theme, so a Lua app draws its window frame in the colours
       every other window uses. Without these it can only spell the classic
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
