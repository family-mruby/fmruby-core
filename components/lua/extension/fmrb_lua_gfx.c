#include "lua.h"
#include "lauxlib.h"
#include "fmrb_app.h"
#include "fmrb_gfx.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_msg.h"
#include "fmrb_log.h"
#include <string.h>

static const char *TAG = "lua_gfx";

// Userdata type for graphics context
typedef struct {
    fmrb_gfx_context_t ctx;
    fmrb_canvas_handle_t canvas_id;
} lua_gfx_data;

// Helper function to send GFX command message to Host Task
static fmrb_err_t send_gfx_command(const gfx_cmd_t *cmd) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        FMRB_LOGE(TAG, "Failed to get current task context");
        return FMRB_ERR_INVALID_STATE;
    }

    fmrb_msg_t msg = {
        .type = FMRB_MSG_TYPE_APP_GFX,
        .src_pid = ctx->app_id,
        .size = sizeof(gfx_cmd_t)
    };
    memcpy(msg.data, cmd, sizeof(gfx_cmd_t));

    fmrb_err_t ret = fmrb_msg_send(PROC_ID_HOST, &msg, 100);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to send graphics command: %d", ret);
    }
    return ret;
}

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

    // Send GFX command to Host Task
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_RECT,
        .canvas_id = data->canvas_id,
        .params.rect = {
            .rect = {.x = (int16_t)x, .y = (int16_t)y, .width = (uint16_t)w, .height = (uint16_t)h},
            .color = (fmrb_color_t)color,
            .filled = true
        }
    };

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        return luaL_error(L, "fillRect failed: %d", ret);
    }

    lua_pushvalue(L, 1);  // Return self
    return 1;
}

// gfx:drawString(text, x, y, color)
static int lua_gfx_draw_string(lua_State* L) {
    lua_gfx_data *data = (lua_gfx_data *)luaL_checkudata(L, 1, "FmrbGfx");
    const char *text = luaL_checkstring(L, 2);
    int x = luaL_checkinteger(L, 3);
    int y = luaL_checkinteger(L, 4);
    int color = luaL_checkinteger(L, 5);

    if (!data || !data->ctx) {
        return luaL_error(L, "Graphics not initialized");
    }

    // Send GFX command to Host Task
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_TEXT,
        .canvas_id = data->canvas_id,
        .params.text = {
            .x = (int16_t)x,
            .y = (int16_t)y,
            .color = (fmrb_color_t)color,
            .font_size = FMRB_FONT_SIZE_MEDIUM
        }
    };

    // Copy text (truncate if too long)
    strncpy(cmd.params.text.text, text, sizeof(cmd.params.text.text) - 1);
    cmd.params.text.text[sizeof(cmd.params.text.text) - 1] = '\0';

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        return luaL_error(L, "drawString failed: %d", ret);
    }

    lua_pushvalue(L, 1);  // Return self
    return 1;
}

// gfx:present(x, y)
static int lua_gfx_present(lua_State* L) {
    lua_gfx_data *data = (lua_gfx_data *)luaL_checkudata(L, 1, "FmrbGfx");
    int x = luaL_optinteger(L, 2, 0);
    int y = luaL_optinteger(L, 3, 0);

    if (!data || !data->ctx) {
        return luaL_error(L, "Graphics not initialized");
    }

    // Send GFX command to Host Task
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_PRESENT,
        .canvas_id = data->canvas_id,
        .params.present = {
            .x = (int16_t)x,
            .y = (int16_t)y,
            .transparent_color = 0xFF  // No transparency
        }
    };

    fmrb_err_t ret = send_gfx_command(&cmd);
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

    // Send GFX command to Host Task
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_CLEAR,
        .canvas_id = data->canvas_id,
        .params.clear.color = (fmrb_color_t)color
    };

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        return luaL_error(L, "clear failed: %d", ret);
    }

    lua_pushvalue(L, 1);  // Return self
    return 1;
}

// Method table
static const luaL_Reg gfx_methods[] = {
    {"fillRect", lua_gfx_fill_rect},
    {"drawString", lua_gfx_draw_string},
    {"present", lua_gfx_present},
    {"clear", lua_gfx_clear},
    {NULL, NULL}
};

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

    fmrb_canvas_handle_t canvas_id = FMRB_CANVAS_SCREEN;

    // Get global graphics context
    fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
    if (!gfx_ctx) {
        return luaL_error(L, "Graphics context not initialized");
    }

    // Create canvas for app window
    fmrb_gfx_err_t ret = fmrb_gfx_create_canvas(
        gfx_ctx,
        (uint16_t)width,
        (uint16_t)height,
        &canvas_id
    );

    if (ret != FMRB_GFX_OK) {
        return luaL_error(L, "Failed to create canvas: %d", ret);
    }

    FMRB_LOGI(TAG, "Created canvas %u (%dx%d) for app %s",
             canvas_id, width, height, ctx->app_name);

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
    {"createCanvas", lua_app_create_canvas},
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

    // Add color constants
    lua_pushinteger(L, 0x00);  // Black
    lua_setfield(L, -2, "COLOR_BLACK");
    lua_pushinteger(L, 0xFF);  // White
    lua_setfield(L, -2, "COLOR_WHITE");
    lua_pushinteger(L, 0xE0);  // Red
    lua_setfield(L, -2, "COLOR_RED");
    lua_pushinteger(L, 0x1C);  // Green
    lua_setfield(L, -2, "COLOR_GREEN");
    lua_pushinteger(L, 0x03);  // Blue
    lua_setfield(L, -2, "COLOR_BLUE");
    lua_pushinteger(L, 0xFC);  // Yellow
    lua_setfield(L, -2, "COLOR_YELLOW");
    lua_pushinteger(L, 0xE3);  // Magenta
    lua_setfield(L, -2, "COLOR_MAGENTA");
    lua_pushinteger(L, 0x1F);  // Cyan
    lua_setfield(L, -2, "COLOR_CYAN");

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

    lua_setglobal(L, "FmrbApp");

    FMRB_LOGI(TAG, "FmrbGfx and FmrbApp modules registered to Lua");
}
