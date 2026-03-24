#pragma once

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Sprite (logical) resolution for Atom Display
// HDMI panel performs hardware scaling to output resolution.
#define M5GFX_SPRITE_WIDTH  480
#define M5GFX_SPRITE_HEIGHT 320

// Canvas color depth (8-bit RGB332, natively supported by Panel_M5HDMI)
#define M5GFX_CANVAS_COLOR_DEPTH 8
#define M5GFX_CANVAS_BPP         1   // bytes per pixel for RGB332

// Canvas memory pool configuration
// Each slot = M5GFX_SPRITE_WIDTH * M5GFX_SPRITE_HEIGHT * BPP
#define M5GFX_CANVAS_SLOT_SIZE  (M5GFX_SPRITE_WIDTH * M5GFX_SPRITE_HEIGHT * M5GFX_CANVAS_BPP)
#define M5GFX_CANVAS_MAX_SLOTS  32  // 16 canvases x 2 buffers (draw + render)

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
