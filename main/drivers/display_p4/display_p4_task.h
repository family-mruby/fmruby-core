#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
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
 * Called from the touch task loop. Serialized with all other bus
 * traffic by the internal I2C service mutex.
 */
void display_p4_poll_headphone(void);

/**
 * @brief Shared-bus I2C service (Tab5 internal bus, GPIO31/32).
 *
 * LovyanGFX drives this I2C controller at register level, so runtime
 * access from other modules must go through these wrappers: they use
 * lgfx's I2C path and serialize against the GT911 touch reads with an
 * internal mutex. Callable from any task once the display is ready
 * (returns FMRB_ERR_INVALID_STATE before that). len is limited to 255
 * bytes per transaction.
 */
fmrb_err_t display_p4_i2c_write(uint8_t addr, const uint8_t *data,
                                size_t len, uint32_t freq);
fmrb_err_t display_p4_i2c_read(uint8_t addr, uint8_t *data,
                               size_t len, uint32_t freq);
fmrb_err_t display_p4_i2c_write_reg8(uint8_t addr, uint8_t reg,
                                     uint8_t value, uint32_t freq);

#ifdef __cplusplus
}
#endif
