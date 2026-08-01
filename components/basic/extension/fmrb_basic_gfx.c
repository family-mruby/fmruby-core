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
#include "basic_assets.h"
#include "fmrb_gfx_cmd.h"
#include "fmrb_log.h"
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

static void fill_rect(basic_console_ctx_t* console, int16_t x, int16_t y,
                      uint16_t w, uint16_t h, uint8_t color) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_rect(&cmd, console->canvas_id, x, y, w, h,
                      (fmrb_color_t)color, true);
    fmrb_gfx_submit(&cmd);
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
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_set_sprite_image_target(&cmd, console->canvas_id, image_id);
    fmrb_gfx_submit(&cmd);
}

/// RGB332 for colour index 0-3 of an attribute group; 0 is the backdrop.
static uint8_t index_rgb(const basic_console_ctx_t* console, const uint8_t table[4][3],
                         uint8_t group, uint8_t index);

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
    uint16_t glyph[BASIC_SCREEN_CELL_H];
    basic_assets_glyph(console->text_table_a, code, glyph);
    for (uint8_t row = 0; row < BASIC_SCREEN_CELL_H; row++) {
        const uint16_t bits = glyph[row];
        uint8_t col = 0;
        while (bits != 0 && col < BASIC_SCREEN_CELL_W) {
            const uint8_t index = (uint8_t)((bits >> (14 - 2 * col)) & 3);
            if (index == 0) {
                col++;
                continue;  // backdrop: already filled
            }
            // Runs of one colour become one rectangle, which is what keeps a
            // full screen repaint inside the link budget (B3 report).
            uint8_t run = 1;
            while (col + run < BASIC_SCREEN_CELL_W &&
                   ((bits >> (14 - 2 * (col + run))) & 3) == index) {
                run++;
            }
            fill_rect(console, (int16_t)(sx + col), (int16_t)(sy + row), run, 1,
                      index_rgb(console, console->attr_rgb, attr, index));
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

    const int16_t px = (int16_t)(console->pad_x + x * BASIC_SCREEN_CELL_W);
    const int16_t py = (int16_t)(console->pad_y + y * BASIC_SCREEN_CELL_H);

    if (ensure_glyph(console, code, attr)) {
        gfx_cmd_t cmd;
        fmrb_gfx_cmd_draw_tile(
            &cmd, console->canvas_id, console->sheet_id[attr],
            (int16_t)((code % GLYPH_SHEET_COLS) * BASIC_SCREEN_CELL_W),
            (int16_t)((code / GLYPH_SHEET_COLS) * BASIC_SCREEN_CELL_H),
            BASIC_SCREEN_CELL_W, BASIC_SCREEN_CELL_H, px, py);
        fmrb_gfx_submit(&cmd);
    } else {
        // No sheet (out of image memory): fall back to drawing the runs.
        fill_rect(console, px, py, BASIC_SCREEN_CELL_W, BASIC_SCREEN_CELL_H,
                  console->backdrop_rgb);
        uint16_t glyph[BASIC_SCREEN_CELL_H];
        basic_assets_glyph(console->text_table_a, code, glyph);
        for (uint8_t row = 0; row < BASIC_SCREEN_CELL_H; row++) {
            const uint16_t bits = glyph[row];
            uint8_t col = 0;
            while (bits != 0 && col < BASIC_SCREEN_CELL_W) {
                const uint8_t index = (uint8_t)((bits >> (14 - 2 * col)) & 3);
                if (index == 0) {
                    col++;
                    continue;
                }
                uint8_t run = 1;
                while (col + run < BASIC_SCREEN_CELL_W &&
                       ((bits >> (14 - 2 * (col + run))) & 3) == index) {
                    run++;
                }
                fill_rect(console, (int16_t)(px + col), (int16_t)(py + row), run, 1,
                          index_rgb(console, console->attr_rgb, attr, index));
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
        fill_rect(console, console->pad_x, console->pad_y, BASIC_SCREEN_W, BASIC_SCREEN_H,
                  console->backdrop_rgb);
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
    if (!console || (!console->dirty && !console->sprites_moved)) {
        return;
    }
    console->dirty = false;
    console->sprites_moved = false;

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_present(&cmd, console->canvas_id, console->origin_x,
                         console->origin_y, 0xFF);  // 0xFF = no transparency
    fmrb_gfx_submit(&cmd);
}

/**
 * FILTER tint colours, as RGB332 components (r 0-7, g 0-7, b 0-3).
 *
 * v3_spec numbers them 1=red 2=green 3=yellow 4=blue 5=magenta 6=cyan 7=white,
 * index 0 being no filter. The mixing below (half way towards the tint) is a
 * placeholder: the real machine tints through the PPU, and the values here are
 * meant to be replaced once a recording of it is available. Both this table and
 * apply_filter() are the only things to change then.
 */
static const uint8_t FILTER_TINT[8][3] = {
    {0, 0, 0},  // 0: unused, no filter
    {7, 0, 0},  // 1: red
    {0, 7, 0},  // 2: green
    {7, 7, 0},  // 3: yellow
    {0, 0, 3},  // 4: blue
    {7, 0, 3},  // 5: magenta
    {0, 7, 3},  // 6: cyan
    {7, 7, 3},  // 7: white
};

/// Mix a colour a quarter of the way towards the filter tint, in RGB332 space.
/// The spec calls the effect "lightly coloured", so the weight is small; it is
/// part of the placeholder and moves with the table above.
static uint8_t apply_filter(uint8_t rgb, uint8_t filter) {
    if (filter == 0 || filter > 7) {
        return rgb;
    }
    const uint8_t r = (uint8_t)((rgb >> 5) & 0x07);
    const uint8_t g = (uint8_t)((rgb >> 2) & 0x07);
    const uint8_t b = (uint8_t)(rgb & 0x03);
    const uint8_t tr = (uint8_t)((3 * r + FILTER_TINT[filter][0] + 2) / 4);
    const uint8_t tg = (uint8_t)((3 * g + FILTER_TINT[filter][1] + 2) / 4);
    const uint8_t tb = (uint8_t)((3 * b + FILTER_TINT[filter][2] + 2) / 4);
    return (uint8_t)((tr << 5) | (tg << 2) | tb);
}

/// Forget what is on the canvas so the next cell updates all repaint.
static void invalidate_cells(basic_console_ctx_t* console) {
    // 0xFF is not a valid attribute (they are masked to 0-3), so every cell
    // compares as changed.
    memset(console->drawn_attr, 0xFF, sizeof(console->drawn_attr));
}

/// Turn the stored text colour codes into RGB332 through the code table and
/// FILTER. FILTER is a BG plane effect (v3_spec), so the sprite colours skip it.
///
/// This drops both text caches, which is expensive: the glyph sheet has to be
/// re-rendered a cell at a time (a dozen link commands each) and every cell on
/// screen redraws. Only call it when the text palette actually changed - a
/// sprite palette write must not land here.
static void recompute_text_palette(basic_console_ctx_t* console) {
    console->backdrop_rgb =
        apply_filter(COLOR_RGB332[console->backdrop_code & 0x3F], console->filter_color);
    for (uint8_t group = 0; group < 4; group++) {
        for (uint8_t i = 0; i < 3; i++) {
            console->attr_rgb[group][i] = apply_filter(
                COLOR_RGB332[console->attr_code[group][i] & 0x3F], console->filter_color);
        }
    }
    invalidate_glyphs(console);
    invalidate_cells(console);
}

/// Sprite colours only. Sprites carry their own images, so nothing about the
/// text planes is affected.
static void recompute_sprite_palette(basic_console_ctx_t* console) {
    for (uint8_t group = 0; group < 4; group++) {
        for (uint8_t i = 0; i < 3; i++) {
            console->sprite_rgb[group][i] =
                COLOR_RGB332[console->sprite_code[group][i] & 0x3F];
        }
    }
}

static uint8_t index_rgb(const basic_console_ctx_t* console, const uint8_t table[4][3],
                         uint8_t group, uint8_t index) {
    if (index == 0) {
        return console->backdrop_rgb;
    }
    return table[group & 3][(index - 1) & 3];
}

void basic_console_set_filter(void* user_data, uint8_t color) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console || color > 7 || console->filter_color == color) {
        return;
    }
    console->filter_color = color;
    // The interpreter repaints every cell right after this (screen_refresh),
    // which is what puts the new colours on the canvas. FILTER is a BG plane
    // effect, so the sprite colours are untouched.
    recompute_text_palette(console);
    FMRB_LOGI(TAG, "FILTER %u", color);
}

void basic_console_set_palette(void* user_data, uint8_t attr, uint8_t backdrop,
                               uint8_t c1, uint8_t c2, uint8_t c3) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console || attr > 3) {
        return;
    }
    // The three colours of the group are the ink indices 1-3; index 0 is the
    // shared backdrop. Single colour artwork draws in index 3, which is why
    // that one is the "text colour" a program sets with COLOR.
    // Programs re-issue the same palette every frame (a sprite redefinition in
    // the main loop is enough), so bail out when nothing moved rather than
    // throwing away the glyph sheet and repainting the screen.
    if (console->attr_code[attr][0] == c1 && console->attr_code[attr][1] == c2 &&
        console->attr_code[attr][2] == c3 && console->backdrop_code == backdrop) {
        return;
    }
    console->attr_code[attr][0] = c1;
    console->attr_code[attr][1] = c2;
    console->attr_code[attr][2] = c3;
    console->backdrop_code = backdrop;
    recompute_text_palette(console);
}

void basic_console_set_sprite_palette(void* user_data, uint8_t attr, uint8_t c1,
                                      uint8_t c2, uint8_t c3) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console || attr > 3) {
        return;
    }
    if (console->sprite_code[attr][0] == c1 && console->sprite_code[attr][1] == c2 &&
        console->sprite_code[attr][2] == c3) {
        return;
    }
    console->sprite_code[attr][0] = c1;
    console->sprite_code[attr][1] = c2;
    console->sprite_code[attr][2] = c3;
    recompute_sprite_palette(console);
    // Sprite images hold their colours; redraw them with the new palette.
    for (uint8_t i = 0; i < BASIC_SPRITE_SLOTS; i++) {
        console->sprites[i].repaint = true;
    }
}

// --- sprite plane ----------------------------------------------------------

static void sprite_instance_visible(basic_console_ctx_t* console, uint16_t instance_id,
                                    bool visible) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_sprite_instance_set_visible(&cmd, console->canvas_id,
                                             instance_id, visible);
    fmrb_gfx_submit(&cmd);
}

/// Drop a slot's instance and artwork, so it can be rebuilt from scratch.
static void release_sprite_slot(basic_console_ctx_t* console,
                                typeof(console->sprites[0])* slot) {
    if (slot->instance_id != 0) {
        gfx_cmd_t cmd;
        fmrb_gfx_cmd_delete_sprite_instance(&cmd, console->canvas_id,
                                            slot->instance_id);
        fmrb_gfx_submit(&cmd);
        slot->instance_id = 0;
    }
    for (uint8_t f = 0; f < 2; f++) {
        if (slot->image_id[f] == 0) {
            continue;
        }
        gfx_cmd_t cmd;
        fmrb_gfx_cmd_delete_sprite_image(&cmd, console->canvas_id,
                                         slot->image_id[f]);
        fmrb_gfx_submit(&cmd);
        slot->image_id[f] = 0;
    }
    slot->visible = false;
    slot->frame_index = 0;
    slot->pos_valid = false;
}

/// Paint one animation frame's image from the tile bitmaps, honouring flips.
static void draw_sprite_image(basic_console_ctx_t* console, uint16_t image_id,
                              const basic_sprite_view* sprite, uint8_t frame) {
    const uint8_t size = sprite->size16 ? 2 : 1;  // tiles per side
    const uint8_t* tiles = sprite->frame_tiles[frame];

    set_image_target(console, image_id);
    // Transparent background: colour 0 is the image's transparent key.
    fill_rect(console, 0, 0, (uint16_t)(size * BASIC_SCREEN_CELL_W),
              (uint16_t)(size * BASIC_SCREEN_CELL_H), 0);

    for (uint8_t t = 0; t < (uint8_t)(size * size); t++) {
        // 16x16 characters are stored top left, top right, bottom left,
        // bottom right (core_spec sec 8).
        const uint8_t tile_col = (uint8_t)(t % size);
        const uint8_t tile_row = (uint8_t)(t / size);
        uint16_t glyph[BASIC_SCREEN_CELL_H];
        basic_assets_glyph(sprite->table_a, tiles[t], glyph);
        for (uint8_t row = 0; row < BASIC_SCREEN_CELL_H; row++) {
            const uint16_t bits = glyph[row];
            uint8_t col = 0;
            while (col < BASIC_SCREEN_CELL_W) {
                const uint8_t index = (uint8_t)((bits >> (14 - 2 * col)) & 3);
                if (index == 0) {
                    col++;
                    continue;  // transparent
                }
                // Runs of one colour become one rectangle, the same way the
                // glyph sheet is built. Emitting a command per pixel put a
                // 16x16 sprite at over a hundred link commands per repaint,
                // and a program that redefines its sprite every frame (the
                // Dodge sample does) then filled the host queue and delayed
                // HID events.
                uint8_t run = 1;
                while (col + run < BASIC_SCREEN_CELL_W &&
                       ((bits >> (14 - 2 * (col + run))) & 3) == index) {
                    run++;
                }
                // Sprites are drawn from the sprite palette, and index 0 is
                // transparency rather than the backdrop.
                const uint8_t fg =
                    console->sprite_rgb[sprite->attr & 3][(index - 1) & 3];
                uint8_t px = (uint8_t)(tile_col * BASIC_SCREEN_CELL_W + col);
                uint8_t py = (uint8_t)(tile_row * BASIC_SCREEN_CELL_H + row);
                if (sprite->flip_y) {
                    py = (uint8_t)(size * BASIC_SCREEN_CELL_H - 1 - py);
                }
                if (sprite->flip_x) {
                    // The run mirrors as a block: its rightmost pixel becomes
                    // the leftmost, so shift the origin by the run length.
                    px = (uint8_t)(size * BASIC_SCREEN_CELL_W - px - run);
                }
                fill_rect(console, px, py, run, 1, fg);
                col = (uint8_t)(col + run);
            }
        }
    }
    set_image_target(console, 0);
}

void basic_console_sprite_update(void* user_data, const basic_sprite_view* sprite) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console || sprite->index >= BASIC_SPRITE_SLOTS) {
        return;
    }
    typeof(console->sprites[0])* slot = &console->sprites[sprite->index];

    if (!sprite->defined) {
        if (slot->instance_id != 0) {
            sprite_instance_visible(console, slot->instance_id, false);
            slot->visible = false;
        }
        return;
    }

    fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
    if (!gfx_ctx) {
        return;
    }

    const uint8_t frames = sprite->frame_count ? sprite->frame_count : 1;

    // An instance fixes its frame set when it is created and an image its
    // dimensions when it is allocated, so either change means starting over.
    if (slot->frame_count != frames || slot->size16 != sprite->size16) {
        release_sprite_slot(console, slot);
        slot->frame_count = frames;
        slot->size16 = sprite->size16;
    }

    // The artwork only has to be repainted when it actually changes; a sprite
    // that merely moves, or steps its walk cycle, keeps what it has.
    bool need_image = slot->repaint || slot->attr != (sprite->attr & 3) ||
                      slot->table_a != sprite->table_a ||
                      slot->flip_x != sprite->flip_x ||
                      slot->flip_y != sprite->flip_y;
    for (uint8_t f = 0; f < frames && !need_image; f++) {
        need_image = (slot->image_id[f] == 0) ||
                     (slot->base_tile[f] != sprite->frame_tiles[f][0]);
    }

    if (need_image) {
        const uint16_t dim =
            (uint16_t)((sprite->size16 ? 2 : 1) * BASIC_SCREEN_CELL_W);
        for (uint8_t f = 0; f < frames; f++) {
            if (slot->image_id[f] == 0) {
                slot->image_id[f] = fmrb_gfx_create_sprite_image(
                    gfx_ctx, console->canvas_id, dim, dim, 0, true);
                if (slot->image_id[f] == 0) {
                    return;
                }
            }
            draw_sprite_image(console, slot->image_id[f], sprite, f);
            slot->base_tile[f] = sprite->frame_tiles[f][0];
        }
        slot->attr = (uint8_t)(sprite->attr & 3);
        slot->table_a = sprite->table_a;
        slot->flip_x = sprite->flip_x;
        slot->flip_y = sprite->flip_y;
        slot->repaint = false;
    }

    if (slot->instance_id == 0) {
        slot->instance_id = fmrb_gfx_create_sprite_instance(
            gfx_ctx, console->canvas_id, slot->image_id, frames, 0, 0, 1);
        if (slot->instance_id == 0) {
            return;
        }
        slot->visible = true;    // instances start visible
        slot->frame_index = 0;   // and on their first frame
        slot->pos_valid = false; // and at wherever it was created
    }

    // Anything that actually reached the graphics side needs the canvas
    // presented again; a sprite that was notified but did not change does not.
    bool changed = need_image;

    // Stepping the walk cycle is a frame switch, not a repaint. Painting the
    // pose again cost one command per run of pixels, several hundred per
    // second once a MOVE character was walking, which filled the host queue
    // and left HID events waiting behind it.
    const uint8_t frame =
        (uint8_t)(sprite->frame_index < frames ? sprite->frame_index : 0);
    if (frame != slot->frame_index) {
        gfx_cmd_t cmd;
        fmrb_gfx_cmd_sprite_instance_set_frame(&cmd, console->canvas_id,
                                               slot->instance_id, frame);
        fmrb_gfx_submit(&cmd);
        slot->frame_index = frame;
        changed = true;
    }

    // Sprite plane coordinates sit 16 / 24 dots outside the text plane
    // (core_spec sec 8), and the canvas is the text plane.
    const int16_t px = (int16_t)(console->pad_x + sprite->x - 16);
    const int16_t py = (int16_t)(console->pad_y + sprite->y - 24);
    // A DEF MOVE character is notified once per frame whether or not it
    // travelled, so resending a position it already has is pure traffic: on
    // hardware this was the largest command type by a wide margin.
    if (!slot->pos_valid || px != slot->x || py != slot->y) {
        gfx_cmd_t move;
        fmrb_gfx_cmd_sprite_instance_move(&move, console->canvas_id,
                                          slot->instance_id, px, py);
        fmrb_gfx_submit(&move);
        slot->x = px;
        slot->y = py;
        slot->pos_valid = true;
        changed = true;
    }

    const bool want_visible = sprite->visible && console->sprite_plane_on;
    if (want_visible != slot->visible) {
        sprite_instance_visible(console, slot->instance_id, want_visible);
        slot->visible = want_visible;
        changed = true;
    }

    // Sprites are composited when the canvas is presented, so a sprite that
    // moved needs a present as much as a changed cell does. Without this the
    // screen only refreshed when the text happened to change: a game whose
    // score sits still looked frozen, and its controls looked dead.
    if (changed) {
        console->sprites_moved = true;
    }
}

void basic_console_set_charset(void* user_data, bool table_a) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console || console->text_table_a == table_a) {
        return;
    }
    console->text_table_a = table_a;
    // The cached glyphs belong to the old table.
    invalidate_glyphs(console);
    memset(console->drawn_code, 0xFF, sizeof(console->drawn_code));
}

void basic_console_sprite_plane(void* user_data, bool on) {
    basic_console_ctx_t* console = (basic_console_ctx_t*)user_data;
    if (!console) {
        return;
    }
    console->sprite_plane_on = on;
    for (int i = 0; i < BASIC_SPRITE_SLOTS; i++) {
        if (console->sprites[i].instance_id != 0 && !on && console->sprites[i].visible) {
            sprite_instance_visible(console, console->sprites[i].instance_id, false);
            console->sprites[i].visible = false;
        }
    }
    console->sprites_moved = true;
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

    // Pick up edited character sheets on every app start (basic_assets.h).
    basic_assets_reload();

    console->app_ctx = ctx;
    // Default palette until the interpreter pushes its own: white on black.
    console->filter_color = 0;
    console->backdrop_code = 15;
    for (int group = 0; group < 4; group++) {
        for (int i = 0; i < 3; i++) {
            console->attr_code[group][i] = 0x30;
            console->sprite_code[group][i] = 0x30;
        }
    }
    recompute_text_palette(console);
    recompute_sprite_palette(console);

    fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
    if (!gfx_ctx) {
        FMRB_LOGE(TAG, "Graphics context not initialized");
        return FMRB_ERR_INVALID_STATE;
    }

    // A fullscreen .bas app takes the whole frame: its canvas covers the window
    // and the 224x192 text plane is centred inside it, so the surround is black
    // instead of showing the desktop wallpaper around the plane. A windowed app
    // keeps a plane sized canvas at its window position.
    if (ctx->fullscreen) {
        const int32_t free_x = (int32_t)ctx->window_width - BASIC_SCREEN_W;
        const int32_t free_y = (int32_t)ctx->window_height - BASIC_SCREEN_H;
        console->canvas_w = ctx->window_width;
        console->canvas_h = ctx->window_height;
        console->pad_x = (int16_t)(free_x > 0 ? free_x / 2 : 0);
        console->pad_y = (int16_t)(free_y > 0 ? free_y / 2 : 0);
        console->origin_x = 0;
        console->origin_y = 0;
    } else {
        console->canvas_w = BASIC_SCREEN_W;
        console->canvas_h = BASIC_SCREEN_H;
        console->pad_x = 0;
        console->pad_y = 0;
        console->origin_x = (int16_t)ctx->window_pos_x;
        console->origin_y = (int16_t)ctx->window_pos_y;
    }

    fmrb_gfx_err_t gfx_ret = fmrb_gfx_create_canvas(
        gfx_ctx,
        console->canvas_w,
        console->canvas_h,
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

    memset(console->drawn_code, ' ', sizeof(console->drawn_code));
    memset(console->drawn_attr, 0, sizeof(console->drawn_attr));
    if (console->pad_x != 0 || console->pad_y != 0) {
        // The surround is painted once and never touched again.
        fill_rect(console, 0, 0, console->canvas_w, console->canvas_h, 0x00);
    }
    fill_rect(console, console->pad_x, console->pad_y, BASIC_SCREEN_W, BASIC_SCREEN_H,
              console->backdrop_rgb);
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

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_circle(&cmd, console->canvas_id,
                        (int16_t)(console->pad_x + x),
                        (int16_t)(console->pad_y + y), r,
                        (fmrb_color_t)color, filled);
    fmrb_gfx_submit(&cmd);
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

    for (int i = 0; i < BASIC_SPRITE_SLOTS; i++) {
        release_sprite_slot(console, &console->sprites[i]);
        console->sprites[i].frame_count = 0;
    }

    for (int attr = 0; attr < 4; attr++) {
        if (console->sheet_id[attr] != 0) {
            gfx_cmd_t cmd;
            fmrb_gfx_cmd_delete_sprite_image(&cmd, console->canvas_id,
                                             console->sheet_id[attr]);
            fmrb_gfx_submit(&cmd);
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
