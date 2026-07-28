/**
 * @file basic_assets.c
 * @brief BMP sheet loader for the character tables (see basic_assets.h).
 */

#include "basic_assets.h"
#include "basic_font8.h"
#include "basic_tile_a.h"
#include "fmrb_hal_file.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include <stdbool.h>
#include <string.h>

static const char *TAG = "basic_assets";

#define FONT_SHEET_PATH "/usr/share/basic/font_b.bmp"
#define TILE_SHEET_PATH "/usr/share/basic/tile_a.bmp"

#define SHEET_COLS   16
#define CELL         8
/// One glyph is 8 rows of 16 bits (2 bits per pixel), so 4KB for 256 of them.
#define TABLE_BYTES  (256 * 8 * (int)sizeof(uint16_t))
/// 128 pixels at 8bpp is the widest row this loader accepts, plus BMP padding.
#define MAX_ROW_BYTES 132

static uint16_t (*s_font)[8];
static uint16_t (*s_tile)[8];

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool read_at(fmrb_file_t file, uint32_t offset, void *buffer, size_t size) {
    if (fmrb_hal_file_seek(file, (int32_t)offset, FMRB_SEEK_SET) != FMRB_OK) {
        return false;
    }
    size_t bytes_read = 0;
    if (fmrb_hal_file_read(file, buffer, size, &bytes_read) != FMRB_OK) {
        return false;
    }
    return bytes_read == size;
}

/// Pixel index at column x of a packed BMP row.
static uint8_t row_index(const uint8_t *row, uint16_t bpp, int32_t x) {
    if (bpp == 8) {
        return row[x];
    }
    if (bpp == 4) {
        return (x & 1) ? (uint8_t)(row[x >> 1] & 0x0F) : (uint8_t)(row[x >> 1] >> 4);
    }
    return (uint8_t)((row[x >> 3] >> (7 - (x & 7))) & 1);
}

/// Read the pixel rows one at a time; a whole 8bpp sheet would be 16KB of heap.
static bool load_rows(fmrb_file_t file, uint16_t dst[256][8], uint32_t bits_offset,
                      uint32_t stride, uint16_t bpp, bool top_down) {
    uint8_t row[MAX_ROW_BYTES];

    memset(dst, 0, TABLE_BYTES);
    for (int32_t y = 0; y < BASIC_ASSET_SHEET_DIM; y++) {
        const int32_t file_row = top_down ? y : (BASIC_ASSET_SHEET_DIM - 1 - y);
        if (!read_at(file, bits_offset + (uint32_t)file_row * stride, row, stride)) {
            return false;
        }
        for (int32_t x = 0; x < BASIC_ASSET_SHEET_DIM; x++) {
            uint8_t index = row_index(row, bpp, x);
            if (index == 0) {
                continue;  // palette index 0 = off / transparent
            }
            // A 1bpp sheet has no second colour: its ink is index 3, the colour
            // single colour artwork has always been drawn in.
            if (bpp == 1) {
                index = 3;
            } else if (index > 3) {
                // Indices past the four the renderer knows are ink as well; a
                // sheet saved from an editor with a bigger palette still works.
                index = 3;
            }
            const uint8_t code = (uint8_t)((y / CELL) * SHEET_COLS + (x / CELL));
            const uint8_t shift = (uint8_t)(14 - 2 * (x % CELL));
            dst[code][y % CELL] |= (uint16_t)((uint16_t)index << shift);
        }
    }
    return true;
}

static bool load_sheet(const char *path, uint16_t dst[256][8]) {
    fmrb_file_t file = NULL;
    if (fmrb_hal_file_open(path, FMRB_O_RDONLY, &file) != FMRB_OK) {
        return false;  // no sheet on this filesystem: keep the built-in table
    }

    bool ok = false;
    uint8_t header[54];
    if (read_at(file, 0, header, sizeof(header)) &&
        header[0] == 'B' && header[1] == 'M') {
        const uint32_t bits_offset = rd32(header + 10);
        const uint32_t dib_size    = rd32(header + 14);
        const int32_t  width       = (int32_t)rd32(header + 18);
        const int32_t  raw_height  = (int32_t)rd32(header + 22);
        const uint16_t bpp         = rd16(header + 28);
        const uint32_t compression = rd32(header + 30);
        const bool     top_down    = raw_height < 0;
        const int32_t  height      = top_down ? -raw_height : raw_height;
        const uint32_t stride      = (((uint32_t)width * bpp + 31u) / 32u) * 4u;

        if (dib_size < 40 || compression != 0) {
            FMRB_LOGW(TAG, "%s: need an uncompressed BITMAPINFOHEADER BMP", path);
        } else if (width != BASIC_ASSET_SHEET_DIM || height != BASIC_ASSET_SHEET_DIM) {
            FMRB_LOGW(TAG, "%s: sheet must be %dx%d, got %ldx%ld", path,
                      BASIC_ASSET_SHEET_DIM, BASIC_ASSET_SHEET_DIM,
                      (long)width, (long)height);
        } else if ((bpp != 1 && bpp != 4 && bpp != 8) || stride > MAX_ROW_BYTES) {
            FMRB_LOGW(TAG, "%s: need 1, 4 or 8 bits per pixel, got %u", path, bpp);
        } else {
            ok = load_rows(file, dst, bits_offset, stride, bpp, top_down);
            if (!ok) {
                FMRB_LOGW(TAG, "%s: truncated pixel data", path);
            }
        }
    } else {
        FMRB_LOGW(TAG, "%s: not a BMP file", path);
    }

    fmrb_hal_file_close(file);
    return ok;
}

/// Load one sheet into its (lazily allocated) table, or drop back to built-in.
static void reload_one(const char *path, uint16_t (**slot)[8]) {
    uint16_t (*table)[8] = *slot;
    const bool allocated = table != NULL;

    if (!allocated) {
        table = (uint16_t (*)[8])fmrb_sys_malloc(TABLE_BYTES);
        if (!table) {
            return;  // built-in table stays in use
        }
    }

    if (load_sheet(path, table)) {
        *slot = table;
        FMRB_LOGI(TAG, "%s: sheet loaded", path);
        return;
    }

    if (!allocated) {
        fmrb_sys_free(table);
    } else {
        // The sheet was readable on a previous run but is not now; stop using
        // the stale copy so the built-in table is what shows up.
        fmrb_sys_free(table);
        *slot = NULL;
    }
}

void basic_assets_reload(void) {
    reload_one(FONT_SHEET_PATH, &s_font);
    reload_one(TILE_SHEET_PATH, &s_tile);
}

/// Expand one 1bpp row (bit 7 leftmost) into 2bpp, ink = index 3.
static uint16_t promote_row(uint8_t bits) {
    uint16_t out = 0;
    for (uint8_t x = 0; x < CELL; x++) {
        if (bits & (uint8_t)(0x80u >> x)) {
            out |= (uint16_t)(3u << (14 - 2 * x));
        }
    }
    return out;
}

void basic_assets_glyph(bool table_a, uint8_t code, uint16_t out[8]) {
    const uint16_t (*loaded)[8] = table_a ? s_tile : s_font;
    if (loaded) {
        memcpy(out, loaded[code], 8 * sizeof(uint16_t));
        return;
    }
    if (table_a) {
        // Table A is 2bpp in rodata: its placeholder art uses three colours.
        memcpy(out, basic_tile_a[code], 8 * sizeof(uint16_t));
        return;
    }
    // The text font stays 1bpp in rodata (glyphs need no second colour) and is
    // promoted here, which keeps 2KB of flash that a 2bpp copy would spend.
    for (uint8_t row = 0; row < 8; row++) {
        out[row] = promote_row(basic_font8[code][row]);
    }
}
