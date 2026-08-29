#include "fmrb_hal_gpio.h"
#include "fmrb_log_port.h"
#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "fmrb_hal_gpio";

static uint32_t gpio_states[64] = {0};
static bool gpio_configured[64] = {false};

fmrb_err_t fmrb_hal_gpio_reset(fmrb_gpio_num_t gpio_num) {
    if (gpio_num < 0 || gpio_num >= 64) {
        return FMRB_ERR_INVALID_PARAM;
    }

    gpio_states[gpio_num] = 0;
    gpio_configured[gpio_num] = false;
    ESP_LOGI(TAG, "Linux GPIO %d reset", gpio_num);
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_gpio_config(fmrb_gpio_num_t gpio_num,
                                 fmrb_gpio_mode_t mode,
                                 fmrb_gpio_pull_mode_t pull) {
    if (gpio_num < 0 || gpio_num >= 64) {
        return FMRB_ERR_INVALID_PARAM;
    }

    ESP_LOGI(TAG, "Linux GPIO %d configured as %s with pull %d",
             gpio_num,
             mode == FMRB_GPIO_MODE_INPUT ? "INPUT" :
             mode == FMRB_GPIO_MODE_OUTPUT ? "OUTPUT" : "OUTPUT_OD",
             pull);

    gpio_configured[gpio_num] = true;
    return FMRB_OK;
}

fmrb_err_t fmrb_hal_gpio_set_level(fmrb_gpio_num_t gpio_num, uint32_t level) {
    if (gpio_num < 0 || gpio_num >= 64 || !gpio_configured[gpio_num]) {
        return FMRB_ERR_INVALID_PARAM;
    }

    gpio_states[gpio_num] = level ? 1 : 0;
    ESP_LOGI(TAG, "Linux GPIO %d set to %d", gpio_num, gpio_states[gpio_num]);
    return FMRB_OK;
}

int32_t fmrb_hal_gpio_get_level(fmrb_gpio_num_t gpio_num) {
    if (gpio_num < 0 || gpio_num >= 64 || !gpio_configured[gpio_num]) {
        return -1;
    }

    return gpio_states[gpio_num];
}

fmrb_err_t fmrb_hal_gpio_toggle(fmrb_gpio_num_t gpio_num) {
    int32_t current_level = fmrb_hal_gpio_get_level(gpio_num);
    if (current_level < 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    return fmrb_hal_gpio_set_level(gpio_num, current_level ? 0 : 1);
}

fmrb_err_t fmrb_hal_gpio_set_pull_mode(fmrb_gpio_num_t gpio_num, fmrb_gpio_pull_mode_t pull) {
    ESP_LOGI(TAG, "Linux GPIO %d pull mode set to %d", gpio_num, pull);
    return FMRB_OK;
}
