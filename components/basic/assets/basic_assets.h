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
 * accepted. Palette index 0 is "off" (background / transparent); every other
 * index is "on". Four-colour sheets can therefore already be drawn and stored
 * for the eventual 2bpp colour attribute support -- today the renderer only
 * uses one colour per cell, so the indices collapse to a mask.
 */

#pragma once

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

/// Table B glyphs (text screen): 256 entries of 8 rows, bit 7 = leftmost pixel.
const uint8_t (*basic_assets_font(void))[8];

/// Table A tiles (sprite / animation characters), same layout as the glyphs.
const uint8_t (*basic_assets_tile_a(void))[8];

#ifdef __cplusplus
}
#endif
