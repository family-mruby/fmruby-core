#pragma once

#include "fmrb_hal.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FMRB_I2C_OK      0
#define FMRB_I2C_ERROR  -1

/**
 * @brief Initialize I2C bus
 * @param unit I2C port number (0 or 1)
 * @param frequency SCL frequency in Hz
 * @param sda_pin SDA GPIO pin number
 * @param scl_pin SCL GPIO pin number
 * @return FMRB_I2C_OK (0) on success, otherwise the fmrb_err_t of the
 *         refusal: FMRB_ERR_INVALID_PARAM (no such unit), FMRB_ERR_BUSY
 *         (another task owns the unit), FMRB_ERR_NOT_SUPPORTED (the unit is
 *         fixed to other pins on this board), FMRB_ERR_INVALID_STATE (the
 *         driver that lends the unit is not up yet), FMRB_ERR_FAILED (the bus
 *         did not start on these pins)
 */
int fmrb_hal_i2c_init(int unit, uint32_t frequency, int8_t sda_pin, int8_t scl_pin);

/**
 * @brief Release I2C bus owned by the calling task
 * @param unit I2C port number (0 or 1)
 * @return 0 on success, negative on error
 */
int fmrb_hal_i2c_release(int unit);

/**
 * @brief Read from I2C device
 * @return Number of bytes read, or negative on error
 */
int fmrb_hal_i2c_read(int unit, uint8_t addr, uint8_t *dst, size_t len,
                       bool nostop, uint32_t timeout_us);

/**
 * @brief Write to I2C device
 * @return Number of bytes written, or negative on error
 */
int fmrb_hal_i2c_write(int unit, uint8_t addr, uint8_t *src, size_t len,
                        bool nostop, uint32_t timeout_us);

#ifdef __cplusplus
}
#endif
