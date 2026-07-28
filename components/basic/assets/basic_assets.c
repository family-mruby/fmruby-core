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
#define TABLE_BYTES  (256 * 8)
/// 128 pixels at 8bpp is the widest row this loader accepts, plus BMP padding.
#define MAX_ROW_BYTES 132

static uint8_t (*s_font)[8];
static uint8_t (*s_tile)[8];

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
static bool load_rows(fmrb_file_t file, uint8_t dst[256][8], uint32_t bits_offset,
                      uint32_t stride, uint16_t bpp, bool top_down) {
    uint8_t row[MAX_ROW_BYTES];

    memset(dst, 0, TABLE_BYTES);
    for (int32_t y = 0; y < BASIC_ASSET_SHEET_DIM; y++) {
        const int32_t file_row = top_down ? y : (BASIC_ASSET_SHEET_DIM - 1 - y);
        if (!read_at(file, bits_offset + (uint32_t)file_row * stride, row, stride)) {
            return false;
        }
        for (int32_t x = 0; x < BASIC_ASSET_SHEET_DIM; x++) {
            if (row_index(row, bpp, x) == 0) {
                continue;  // palette index 0 = off / transparent
            }
            const uint8_t code = (uint8_t)((y / CELL) * SHEET_COLS + (x / CELL));
            dst[code][y % CELL] |= (uint8_t)(0x80u >> (x % CELL));
        }
    }
    return true;
}

static bool load_sheet(const char *path, uint8_t dst[256][8]) {
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
static void reload_one(const char *path, uint8_t (**slot)[8]) {
    uint8_t (*table)[8] = *slot;
    const bool allocated = table != NULL;

    if (!allocated) {
        table = (uint8_t (*)[8])fmrb_sys_malloc(TABLE_BYTES);
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

const uint8_t (*basic_assets_font(void))[8] {
    return s_font ? (const uint8_t (*)[8])s_font : basic_font8;
}

const uint8_t (*basic_assets_tile_a(void))[8] {
    return s_tile ? (const uint8_t (*)[8])s_tile : basic_tile_a;
}
