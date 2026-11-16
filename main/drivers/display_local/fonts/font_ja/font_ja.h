#pragma once

// Where the display task's efontJA fonts (16 px, 12 px, 12 px bold) come from
// (doc/flash_layout F4).
//
// - ESP32-P4: a partition of their own (font_ja), mapped at boot and handed
//   to LovyanGFX in place (font_ja_partition.cpp). Not copied to RAM.
// - Everywhere else the display task runs (the native program, the browser
//   build, the S3 boards that draw on their own panel): linked into the
//   program from M5GFX as before (font_ja_builtin.cpp).
//
// When the partition is missing or holds another format, the P4 logs why and
// every Japanese size is drawn in misaki 8, which stays in the firmware.

#include <stddef.h>

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Find the fonts. Call once at boot, before the display task draws.
 *
 * @return FMRB_OK when the efontJA fonts are there; otherwise the reason
 *         has been logged and font_ja_get answers NULL.
 */
fmrb_err_t font_ja_init(void);

/**
 * @brief What the board's flash holds, for the development /status.
 *
 * Writes `,"layout":"storage@0x..+0x..,font_ja@0x..+0x..","font_ja":"..."`
 * on the P4 (the partitions as this firmware sees them, and the font image's
 * version and CRC or why there is none), nothing elsewhere. rake
 * flash:app_sync compares it with the build before an app-only flash.
 *
 * @return the length written (0 on a build without the partition)
 */
size_t font_ja_flash_status(char *out, size_t cap);

#ifdef __cplusplus
}

#include <lgfx/v1/lgfx_fonts.hpp>

enum font_ja_id_t {
    FONT_JA_16 = 0,
    FONT_JA_12,
    FONT_JA_12_BOLD,
    FONT_JA_COUNT
};

/**
 * @brief The efontJA font for @p id, or nullptr when this board has none
 *        (built with FMRB_FONT_JA=0, or the P4's partition could not be used).
 */
const lgfx::IFont *font_ja_get(font_ja_id_t id);
#endif
