#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Family mruby Modern (ESP32-P4 / Tab5) local display task.
// Renders the gfx command stream (received over the in-process local link)
// to the Tab5 MIPI-DSI panel. Replaces the WROVER/NTSC path of the Retro build.

/**
 * @brief Initialize the Modern display task (Tab5 MIPI-DSI).
 * @return FMRB_OK on success, error code otherwise.
 */
fmrb_err_t display_p4_task_init(void);

/**
 * @brief Deinitialize the Modern display task.
 * @return FMRB_OK on success.
 */
fmrb_err_t display_p4_task_deinit(void);

/**
 * @brief Check if the LGFX display is initialized and ready.
 * @return true if g_lcd.init() has completed successfully.
 */
bool display_p4_is_ready(void);

/**
 * @brief Read touch coordinates from the GT911 touch panel.
 *
 * Wraps LovyanGFX g_lcd.getTouch() for cross-file access.
 * Coordinates are in panel space (after rotation).
 *
 * @param out_x Pointer to receive X coordinate
 * @param out_y Pointer to receive Y coordinate
 * @return Number of touch points detected (0 or 1)
 */
int display_p4_get_touch(int16_t *out_x, int16_t *out_y);

/**
 * @brief Poll the headphone jack and gate the speaker amp (PI4IO #1).
 *
 * Must be called from the touch task only: it uses lgfx's I2C helpers,
 * which share the controller with the GT911 transactions and are only
 * safe when serialized in the same task context.
 */
void display_p4_poll_headphone(void);

#ifdef __cplusplus
}
#endif
