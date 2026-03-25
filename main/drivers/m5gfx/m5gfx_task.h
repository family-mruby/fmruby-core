#pragma once

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Display resolution for Atom Display
// HDMI panel performs hardware scaling to output resolution.
#define M5GFX_SPRITE_WIDTH  320
#define M5GFX_SPRITE_HEIGHT 240

// FPGA offscreen Canvas compositing
// FPGA logical resolution is extended vertically to hold offscreen Canvas areas.
// Display shows only the top M5GFX_SPRITE_HEIGHT rows (visible area).
// Each Canvas is drawn to an offscreen region starting at Y = SPRITE_HEIGHT * (slot+1).
// PUSH_CANVAS uses CMD_COPYRECT to copy offscreen → visible area (13 bytes SPI).
#define M5GFX_MAX_OFFSCREEN_CANVASES  3
#define M5GFX_LOGICAL_HEIGHT  (M5GFX_SPRITE_HEIGHT * (1 + M5GFX_MAX_OFFSCREEN_CANVASES))
// 320 x 960 x 60 = 18,432,000 <= 55,296,000 constraint OK

// Canvas color depth (8-bit RGB332, natively supported by Panel_M5HDMI)
#define M5GFX_CANVAS_COLOR_DEPTH 8
#define M5GFX_CANVAS_BPP         1   // bytes per pixel for RGB332

// Canvas PSRAM pool is no longer needed for FPGA offscreen approach.
// Keep definitions for backward compat but pool is unused.
#define M5GFX_CANVAS_SLOT_SIZE  (M5GFX_SPRITE_WIDTH * M5GFX_SPRITE_HEIGHT * M5GFX_CANVAS_BPP)
#define M5GFX_CANVAS_MAX_SLOTS  32

/**
 * @brief Initialize M5GFX receiver task
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t m5gfx_task_init(void);

/**
 * @brief Deinitialize M5GFX receiver task
 * @return FMRB_OK on success
 */
fmrb_err_t m5gfx_task_deinit(void);

#ifdef __cplusplus
}
#endif
