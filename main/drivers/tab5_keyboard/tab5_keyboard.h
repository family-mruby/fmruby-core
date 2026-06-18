#pragma once

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// M5Stack Tab5 Keyboard accessory driver (I2C, STM32F030 controller @ 0x6D).
// Reads keys in "character mode" (ASCII + Ctrl/Alt state) and forwards them
// to the host task as HID key events. No touchpad (mouse comes from the Tab5
// body capacitive touch panel, handled by the display task).

/**
 * @brief Initialize the Tab5 Keyboard driver and start its polling task.
 * @return FMRB_OK on success, error code otherwise.
 */
fmrb_err_t tab5_keyboard_init(void);

/**
 * @brief Deinitialize the Tab5 Keyboard driver.
 * @return FMRB_OK on success.
 */
fmrb_err_t tab5_keyboard_deinit(void);

#ifdef __cplusplus
}
#endif
