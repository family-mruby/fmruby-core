#pragma once

#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I2C keyboard driver
 *
 * Starts a polling task that reads keycodes from an I2C keyboard
 * (slave address 0x5F) and sends HID key events to the host task.
 *
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t i2c_keyboard_init(void);

/**
 * @brief Deinitialize I2C keyboard driver
 *
 * @return FMRB_OK on success
 */
fmrb_err_t i2c_keyboard_deinit(void);

#ifdef __cplusplus
}
#endif
