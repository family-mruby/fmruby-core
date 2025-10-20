#pragma once

#include "fmrb_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure GPIO pin
 * @param gpio_num GPIO pin number
 * @param mode GPIO mode (input/output)
 * @param pull Pull mode (none/up/down)
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_gpio_config(fmrb_gpio_num_t gpio_num,
                                 fmrb_gpio_mode_t mode,
                                 fmrb_gpio_pull_mode_t pull);

/**
 * @brief Set GPIO pin level
 * @param gpio_num GPIO pin number
 * @param level Level to set (0 or 1)
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_gpio_set_level(fmrb_gpio_num_t gpio_num, uint32_t level);

/**
 * @brief Get GPIO pin level
 * @param gpio_num GPIO pin number
 * @return Pin level (0 or 1), or -1 on error
 */
int32_t fmrb_hal_gpio_get_level(fmrb_gpio_num_t gpio_num);

/**
 * @brief Toggle GPIO pin level
 * @param gpio_num GPIO pin number
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_gpio_toggle(fmrb_gpio_num_t gpio_num);

#ifdef __cplusplus
}
#endif
