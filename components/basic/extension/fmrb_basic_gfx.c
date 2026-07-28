/**
 * @file fmrb_basic_gfx.c
 * @brief BASIC text screen renderer
 *
 * Mirrors the interpreter's 28x24 shadow buffer onto an fmrb_gfx canvas. A
 * cell is drawn as one background rectangle plus the horizontal runs of the
 * glyph, so no tile sheet has to be uploaded to the graphics side and the
 * colour attribute can change per cell for free.
 */

#include "fmrb_basic_gfx.h"
#include "basic_font8.h"
#include "fmrb_msg.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include <string.h>

static const char *TAG = "basic_gfx";

/**
 * Colour code (0-60) to RGB332.
 *
 * core_spec sec 7 says the codes share the structure of the PPU palette, so
 * the table is the widely used measured RGB set for that palette, reduced to
 * RGB332. Codes 0x0D-0x0F, 0x1D-0x1F and 0x2D-0x2F are the black group.
 */
static const uint8_t COLOR_RGB332[64] = {
    /* 0x00 */ 0x49, 0x01, 0x02, 0x22, 0x40, 0x60, 0x60, 0x40,
    /* 0x08 */ 0x28, 0x08, 0x08, 0x08, 0x05, 0x00, 0x00, 0x00,
    /* 0x10 */ 0x92, 0x0B, 0x2B, 0x4B, 0x83, 0xA2, 0x81, 0x64,
    /* 0x18 */ 0x48, 0x2C, 0x0C, 0x0D, 0x0E, 0x00, 0x00, 0x00,
    /* 0x20 */ 0xFF, 0x5F, 0x6F, 0xAF, 0xEB, 0xEE, 0xED, 0xD5,
    /* 0x28 */ 0x94, 0x78, 0x5C, 0x3D, 0x3E, 0x00, 0x00, 0x00,
    /* 0x30 */ 0xFF, 0xBF, 0xBF, 0xDF, 0xFF, 0xFE, 0xFE, 0xFA,
    /* 0x38 */ 0xDA, 0xBA, 0xBA, 0xBE, 0xBF, 0x00, 0x00, 0x00,
};

// Send graphics command to Host Task (same pattern as fmrb_lua_gfx.c)
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

    fmrb_err_t ret = FMRB_ERR_TIMEOUT;
    for (int retry = 0; retry < 3; retry++) {
        ret = fmrb_msg_send(PROC_ID_HOST, &msg, 5000);
        if (ret == FMRB_OK) {
            break;
        }
        FMRB_LOGW(TAG, "Failed to send graphics command, retry %d/3", retry + 1);
        fmrb_task_delay_ms(100);
    }
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Graphics command dropped after 3 retries");
    }
    return ret;
}

static void fill_rect(basic_console_ctx_t* console, int16_t x, int16_t y,
                      uint16_t w, uint16_t h, uint8_t color) {
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_RECT,
        .canvas_id = console->canvas_id,
        .params.rect = {
            .rect = {.x = x, .y = y, .width = w, .height = h},
            .color = (fmrb_color_t)color,
            .filled = true
        }
    };
    send_gfx_command(&cmd);
}

// --- glyph sheet cache -----------------------------------------------------
//
// The link carries roughly a thousand graphics commands per second, so drawing
// a cell as a dozen rectangles caps a full screen repaint at several seconds
// (measured, see the B3 report). Each glyph is therefore rendered once into a
// per attribute sheet on the graphics side, and cells become one draw_tile.

#define GLYPH_SHEET_COLS 16
#define GLYPH_SHEET_DIM  (GLYPH_SHEET_COLS * BASIC_SCREEN_CELL_W)

static void set_image_target(basic_console_ctx_t* console, uint16_t image_id) {
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_SET_SPRITE_IMAGE_TARGET,
        .canvas_id = console->canvas_id,
        .params.set_sprite_image_target = {.image_id = image_id}
    };
    send_gfx_command(&cmd);
}

/// Draw the glyph into its sheet slot if it is not cached yet.
static bool ensure_glyph(basic_console_ctx_t* console, uint8_t code, uint8_t attr) {
    if (console->sheet_id[attr] == 0) {
        fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
        if (!gfx_ctx) {
            return false;
        }
        console->sheet_id[attr] = fmrb_gfx_create_sprite_image(
            gfx_ctx, console->canvas_id, GLYPH_SHEET_DIM, GLYPH_SHEET_DIM, 0, false);
        if (console->sheet_id[attr] == 0) {
            return false;
        }
        memset(console->sheet_ready[attr], 0, sizeof(console->sheet_ready[attr]));
    }
    if (console->sheet_ready[attr][code >> 3] & (uint8_t)(1u << (code & 7))) {
        return true;
    }

    const int16_t sx = (int16_t)((code % GLYPH_SHEET_COLS) * BASIC_SCREEN_CELL_W);
    const int16_t sy = (int16_t)((code / GLYPH_SHEET_COLS) * BASIC_SCREEN_CELL_H);

    set_image_target(console, console->sheet_id[attr]);
    fill_rect(console, sx, sy, BASIC_SCREEN_CELL_W, BASIC_SCREEN_CELL_H,
              console->backdrop_rgb);
    const uint8_t* glyph = basic_font8[code];
    for (uint8_t row = 0; row < BASIC_SCREEN_CELL_H; row++) {
        uint8_t bits = glyph[row];
        uint8_t col = 0;
        while (bits != 0 && col < BASIC_SCREEN_CELL_W) {
            if ((bits & (0x80 >> col)) == 0) {
                col++;
                continue;
            }
            uint8_t run = 1;
            while (col + run < BASIC_SCREEN_CELL_W && (bits & (0x80 >> (col + run)))) {
                run++;
            }
            fill_rect(console, (int16_t)(sx + col), (int16_t)(sy + row), run, 1,
                      console->attr_rgb[attr]);
            col = (uint8_t)(col + run);
        }
    }
    set_image_target(console, 0);

    console->sheet_ready[attr][code >> 3] |= (uint8_t)(1u << (code & 7));
    return true;
}

/// Forget every cached glyph (the colours behind them changed).
static void invalidate_glyphs(basic_console_ctx_t* console) {
    memset(console->sheet_ready, 0, sizeof(console->sheet_ready));
}

static void mark_dirty(basic_console_ctx_t* console, uint8_t x, uint8_t y) {
    if (!console->dirty) {
        console->dirty = true;
        console->dirty_x0 = x;
        console->dirty_y0 = y;
        console->dirty_x1 = x;
        console->dirty_y1 = y;
        return;
    }
    if (x < console->dirty_x0) console->dirty_x0 = x;
    if (y < console->dirty_y0) console->dirty_y0 = y;
    if (x > console->dirty_x1) console->dirty_x1 = x;
    if (y > console->dirty_y1) console->dirty_y1 = y;
}

void basic_console_draw_cell(void* user_data, uint8_t x, uint8_t y, uint8_t code,
                             uint8_t attr) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console || x >= BASIC_SCREEN_COLS || y >= BASIC_SCREEN_ROWS) {
        return;
    }

    // Redrawing a cell costs about a dozen link commands, so skip the ones
    // that did not actually change (a scroll moves text but leaves most of
    // the screen blank).
    const uint16_t index = (uint16_t)y * BASIC_SCREEN_COLS + x;
    attr &= 3;
    if (console->drawn_code[index] == code && console->drawn_attr[index] == attr) {
        return;
    }
    console->drawn_code[index] = code;
    console->drawn_attr[index] = attr;

    const int16_t px = (int16_t)(x * BASIC_SCREEN_CELL_W);
    const int16_t py = (int16_t)(y * BASIC_SCREEN_CELL_H);

    if (ensure_glyph(console, code, attr)) {
        gfx_cmd_t cmd = {
            .cmd_type = GFX_CMD_DRAW_TILE,
            .canvas_id = console->canvas_id,
            .params.draw_tile = {
                .image_id = console->sheet_id[attr],
                .src_x = (int16_t)((code % GLYPH_SHEET_COLS) * BASIC_SCREEN_CELL_W),
                .src_y = (int16_t)((code / GLYPH_SHEET_COLS) * BASIC_SCREEN_CELL_H),
                .w = BASIC_SCREEN_CELL_W,
                .h = BASIC_SCREEN_CELL_H,
                .dst_x = px,
                .dst_y = py
            }
        };
        send_gfx_command(&cmd);
    } else {
        // No sheet (out of image memory): fall back to drawing the runs.
        fill_rect(console, px, py, BASIC_SCREEN_CELL_W, BASIC_SCREEN_CELL_H,
                  console->backdrop_rgb);
        const uint8_t* glyph = basic_font8[code];
        for (uint8_t row = 0; row < BASIC_SCREEN_CELL_H; row++) {
            uint8_t bits = glyph[row];
            uint8_t col = 0;
            while (bits != 0 && col < BASIC_SCREEN_CELL_W) {
                if ((bits & (0x80 >> col)) == 0) {
                    col++;
                    continue;
                }
                uint8_t run = 1;
                while (col + run < BASIC_SCREEN_CELL_W && (bits & (0x80 >> (col + run)))) {
                    run++;
                }
                fill_rect(console, (int16_t)(px + col), (int16_t)(py + row), run, 1,
                          console->attr_rgb[attr]);
                col = (uint8_t)(col + run);
            }
        }
    }

    mark_dirty(console, x, y);
}

void basic_console_fill(void* user_data, uint8_t code, uint8_t attr) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console) {
        return;
    }
    attr &= 3;

    if (code == ' ') {
        fill_rect(console, 0, 0, BASIC_SCREEN_W, BASIC_SCREEN_H, console->backdrop_rgb);
        memset(console->drawn_code, code, sizeof(console->drawn_code));
        memset(console->drawn_attr, attr, sizeof(console->drawn_attr));
    } else {
        // Any other character still has to be stamped cell by cell.
        for (uint8_t y = 0; y < BASIC_SCREEN_ROWS; y++) {
            for (uint8_t x = 0; x < BASIC_SCREEN_COLS; x++) {
                basic_console_draw_cell(console, x, y, code, attr);
            }
        }
    }
    mark_dirty(console, 0, 0);
    mark_dirty(console, BASIC_SCREEN_COLS - 1, BASIC_SCREEN_ROWS - 1);
}

void basic_console_present(void* user_data) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console || !console->dirty) {
        return;
    }
    console->dirty = false;

    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_PRESENT,
        .canvas_id = console->canvas_id,
        .params.present = {
            .x = console->origin_x,
            .y = console->origin_y,
            .transparent_color = 0xFF  // No transparency
        }
    };
    send_gfx_command(&cmd);
}

void basic_console_set_palette(void* user_data, uint8_t attr, uint8_t backdrop,
                               uint8_t c1, uint8_t c2, uint8_t c3) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console || attr > 3) {
        return;
    }
    (void)c1;
    (void)c2;
    // Text uses the third colour of the group as the glyph colour and the
    // shared backdrop behind it; the other two belong to multi colour tiles
    // (B3, when the BG pattern set arrives).
    console->attr_rgb[attr] = COLOR_RGB332[c3 & 0x3F];
    console->backdrop_rgb = COLOR_RGB332[backdrop & 0x3F];
    invalidate_glyphs(console);
}

fmrb_err_t basic_console_init(basic_console_ctx_t* console,
                              fmrb_app_task_context_t* ctx) {
    if (!console || !ctx) {
        return FMRB_ERR_INVALID_PARAM;
    }

    if (ctx->headless) {
        FMRB_LOGI(TAG, "Headless app, screen output stays in the log");
        return FMRB_ERR_INVALID_STATE;
    }

    console->app_ctx = ctx;
    // Default palette until the interpreter pushes its own: white on black.
    console->backdrop_rgb = COLOR_RGB332[15];
    for (int i = 0; i < 4; i++) {
        console->attr_rgb[i] = COLOR_RGB332[0x30];
    }

    fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
    if (!gfx_ctx) {
        FMRB_LOGE(TAG, "Graphics context not initialized");
        return FMRB_ERR_INVALID_STATE;
    }

    fmrb_gfx_err_t gfx_ret = fmrb_gfx_create_canvas(
        gfx_ctx,
        BASIC_SCREEN_W,
        BASIC_SCREEN_H,
        ctx->z_order,
        false,
        0,
        &console->canvas_id
    );
    if (gfx_ret != FMRB_GFX_OK) {
        FMRB_LOGE(TAG, "Failed to create canvas: %d", gfx_ret);
        return FMRB_ERR_NO_MEMORY;
    }
    ctx->canvas_id = console->canvas_id;

    // The 224x192 text plane sits in the middle of the frame buffer
    // (compat_plan sec 5.1). A windowed .bas app keeps its window position.
    if (ctx->fullscreen) {
        const int32_t free_x = (int32_t)ctx->window_width - BASIC_SCREEN_W;
        const int32_t free_y = (int32_t)ctx->window_height - BASIC_SCREEN_H;
        console->origin_x = (int16_t)(free_x > 0 ? free_x / 2 : 0);
        console->origin_y = (int16_t)(free_y > 0 ? free_y / 2 : 0);
    } else {
        console->origin_x = (int16_t)ctx->window_pos_x;
        console->origin_y = (int16_t)ctx->window_pos_y;
    }

    memset(console->drawn_code, ' ', sizeof(console->drawn_code));
    memset(console->drawn_attr, 0, sizeof(console->drawn_attr));
    fill_rect(console, 0, 0, BASIC_SCREEN_W, BASIC_SCREEN_H, console->backdrop_rgb);
    console->dirty = true;
    console->dirty_x0 = 0;
    console->dirty_y0 = 0;
    console->dirty_x1 = BASIC_SCREEN_COLS - 1;
    console->dirty_y1 = BASIC_SCREEN_ROWS - 1;
    basic_console_present(console);

    FMRB_LOGI(TAG, "BASIC screen canvas %u created (%dx%d) at (%d,%d)",
              console->canvas_id, BASIC_SCREEN_W, BASIC_SCREEN_H,
              console->origin_x, console->origin_y);
    return FMRB_OK;
}

// GFX ops callback: CIRCLE - draw circle on the text plane canvas
static void gfx_ops_circle(void* user_data, int16_t x, int16_t y,
                           int16_t r, uint8_t color, bool filled) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console) return;

    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_CIRCLE,
        .canvas_id = console->canvas_id,
        .params.circle = {
            .x = x,
            .y = y,
            .radius = r,
            .color = (fmrb_color_t)color,
            .filled = filled
        }
    };
    send_gfx_command(&cmd);
    mark_dirty(console, 0, 0);
    mark_dirty(console, BASIC_SCREEN_COLS - 1, BASIC_SCREEN_ROWS - 1);
}

// GFX ops callback: PRESENT - flush canvas to screen
static void gfx_ops_present(void* user_data) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console) return;
    console->dirty = true;
    basic_console_present(console);
}

void basic_console_register_gfx_ops(basic_state_t* state,
                                    basic_console_ctx_t* console) {
    if (!state || !console) return;

    basic_gfx_ops_t ops = {
        .circle = gfx_ops_circle,
        .present = gfx_ops_present,
        .user_data = console
    };
    fmrb_basic_set_gfx_ops(state, &ops);

    FMRB_LOGI(TAG, "Graphics ops registered to BASIC state");
}

void basic_console_destroy(basic_console_ctx_t* console) {
    if (!console) {
        return;
    }

    for (int attr = 0; attr < 4; attr++) {
        if (console->sheet_id[attr] != 0) {
            gfx_cmd_t cmd = {
                .cmd_type = GFX_CMD_DELETE_SPRITE_IMAGE,
                .canvas_id = console->canvas_id,
                .params.delete_sprite_image = {.image_id = console->sheet_id[attr]}
            };
            send_gfx_command(&cmd);
            console->sheet_id[attr] = 0;
        }
    }

    if (console->canvas_id != FMRB_CANVAS_SCREEN) {
        fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
        if (gfx_ctx) {
            fmrb_gfx_delete_canvas(gfx_ctx, console->canvas_id);
        }
    }

    FMRB_LOGI(TAG, "BASIC screen destroyed");
}
