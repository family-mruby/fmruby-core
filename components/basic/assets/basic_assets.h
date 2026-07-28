/**
 * @file basic_assets.h
 * @brief Character table A / B tile bitmaps, overridable by BMP files.
 *
 * The built-in tables (basic_font8.c, basic_tile_a.c) are the fallback. When
 * the matching sheet exists on the filesystem it is loaded instead, so the
 * artwork can be edited in a graphics editor without rebuilding the firmware.
 *
 * Sheet format: uncompressed indexed BMP, 128x128 pixels = 16 x 16 cells of
 * 8x8, character code = row * 16 + column. 1, 4 and 8 bits per pixel are
 * accepted; the palette index of each pixel is the colour index 0-3 the
 * renderer draws (0 = backdrop / transparent, 1-3 the colours of the cell's
 * attribute group). A 1bpp sheet, and the compiled font table, are read as
 * 0 -> 0 and 1 -> 3, so single colour artwork keeps drawing in the colour it
 * always did.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Pixel width and height of an asset sheet.
#define BASIC_ASSET_SHEET_DIM 128

/**
 * @brief Re-read both sheets from the filesystem.
 *
 * Called from basic_console_init(), so editing a sheet and restarting the app
 * is enough to see the new artwork. A missing or malformed file (or a failed
 * allocation) leaves the built-in table in place; that is not an error.
 */
void basic_assets_reload(void);

/**
 * @brief Read one character as 2 bits per pixel.
 *
 * Rows are top first. Within a row the leftmost pixel is the most significant
 * pair: index of pixel x = (row >> (14 - 2 * x)) & 3.
 *
 * Reading through a call rather than exposing the table keeps the font in
 * rodata as 1bpp (it needs no second colour) while table A and any loaded sheet
 * are 2bpp -- callers do not have to care which.
 *
 * @param table_a true for the sprite / animation tiles, false for text glyphs
 * @param code    character code
 * @param out     eight rows, filled by this call
 */
void basic_assets_glyph(bool table_a, uint8_t code, uint16_t out[8]);

#ifdef __cplusplus
}
#endif
