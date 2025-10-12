#include "fmrb_hal_gpio.h"
#include "esp_log.h"

#ifdef FMRB_PLATFORM_LINUX
#include <stdio.h>
#include <stdlib.h>

// Linux implementation - simulation only
static uint32_t gpio_states[64] = {0}; // Support up to 64 GPIO pins
static bool gpio_configured[64] = {false};

#else
#include "driver/gpio.h"
#endif

static const char *TAG = "fmrb_hal_gpio";

fmrb_err_t fmrb_hal_gpio_config(fmrb_gpio_num_t gpio_num,
                                 fmrb_gpio_mode_t mode,
                                 fmrb_gpio_pull_mode_t pull) {
#ifdef FMRB_PLATFORM_LINUX
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
#else
    if (gpio_num < 0 || !GPIO_IS_VALID_GPIO(gpio_num)) {
        return FMRB_ERR_INVALID_PARAM;
    }

    gpio_config_t config = {0};
    config.pin_bit_mask = (1ULL << gpio_num);

    switch (mode) {
        case FMRB_GPIO_MODE_INPUT:
            config.mode = GPIO_MODE_INPUT;
            break;
        case FMRB_GPIO_MODE_OUTPUT:
            config.mode = GPIO_MODE_OUTPUT;
            break;
        case FMRB_GPIO_MODE_OUTPUT_OD:
            config.mode = GPIO_MODE_OUTPUT_OD;
            break;
        default:
            return FMRB_ERR_INVALID_PARAM;
    }

    switch (pull) {
        case FMRB_GPIO_PULL_NONE:
            config.pull_up_en = GPIO_PULLUP_DISABLE;
            config.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case FMRB_GPIO_PULL_UP:
            config.pull_up_en = GPIO_PULLUP_ENABLE;
            config.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case FMRB_GPIO_PULL_DOWN:
            config.pull_up_en = GPIO_PULLUP_DISABLE;
            config.pull_down_en = GPIO_PULLDOWN_ENABLE;
            break;
        default:
            return FMRB_ERR_INVALID_PARAM;
    }

    esp_err_t ret = gpio_config(&config);
    return (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
#endif
}

fmrb_err_t fmrb_hal_gpio_set_level(fmrb_gpio_num_t gpio_num, uint32_t level) {
#ifdef FMRB_PLATFORM_LINUX
    if (gpio_num < 0 || gpio_num >= 64 || !gpio_configured[gpio_num]) {
        return FMRB_ERR_INVALID_PARAM;
    }

    gpio_states[gpio_num] = level ? 1 : 0;
    ESP_LOGI(TAG, "Linux GPIO %d set to %d", gpio_num, gpio_states[gpio_num]);
    return FMRB_OK;
#else
    if (gpio_num < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(gpio_num)) {
        return FMRB_ERR_INVALID_PARAM;
    }

    esp_err_t ret = gpio_set_level(gpio_num, level);
    return (ret == ESP_OK) ? FMRB_OK : FMRB_ERR_FAILED;
#endif
}

int fmrb_hal_gpio_get_level(fmrb_gpio_num_t gpio_num) {
#ifdef FMRB_PLATFORM_LINUX
    if (gpio_num < 0 || gpio_num >= 64 || !gpio_configured[gpio_num]) {
        return -1;
    }

    return gpio_states[gpio_num];
#else
    if (gpio_num < 0 || !GPIO_IS_VALID_GPIO(gpio_num)) {
        return -1;
    }

    return gpio_get_level(gpio_num);
#endif
}

fmrb_err_t fmrb_hal_gpio_toggle(fmrb_gpio_num_t gpio_num) {
    int current_level = fmrb_hal_gpio_get_level(gpio_num);
    if (current_level < 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    return fmrb_hal_gpio_set_level(gpio_num, current_level ? 0 : 1);
}