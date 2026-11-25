#pragma once

#include "fmrb_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

// GPIO types
typedef int fmrb_gpio_num_t;
typedef enum {
    FMRB_GPIO_MODE_INPUT,
    FMRB_GPIO_MODE_OUTPUT,
    FMRB_GPIO_MODE_OUTPUT_OD  // Open drain
} fmrb_gpio_mode_t;

typedef enum {
    FMRB_GPIO_PULL_NONE,
    FMRB_GPIO_PULL_UP,
    FMRB_GPIO_PULL_DOWN
} fmrb_gpio_pull_mode_t;


/**
 * @brief Ask for a pin on behalf of an app's GPIO class (the pin manager's
 *        fmrb_pin_manager_claim_gpio, run on the hw_proxy task for the
 *        calling task)
 * @param gpio_num GPIO pin number
 * @param use fmrb_pin_gpio_use_t: read, input or output
 * @param reset reset the pin to its default state when the caller takes it
 *        now (GPIO.new); a button the system keeps is never reset
 * @return fmrb_pin_gpio_verdict_t as an int; FMRB_PIN_GPIO_OK (0) and
 *         FMRB_PIN_GPIO_OK_SHARED (1) let the caller go on. Nothing on the
 *         pin changes when it is refused. -1 when the pin was granted but
 *         its reset failed.
 */
int fmrb_hal_gpio_claim(fmrb_gpio_num_t gpio_num, int use, bool reset);

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

/**
 * @brief Set GPIO pull mode only (without changing pin direction)
 * @param gpio_num GPIO pin number
 * @param pull Pull mode (none/up/down)
 * @return FMRB_OK on success, error code otherwise
 */
fmrb_err_t fmrb_hal_gpio_set_pull_mode(fmrb_gpio_num_t gpio_num, fmrb_gpio_pull_mode_t pull);

#ifdef __cplusplus
}
#endif
