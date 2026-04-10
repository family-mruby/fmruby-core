#include "fmrb_hal_gpio.h"
#include "hw_proxy.h"
#include "hw_proxy_internal.h"

fmrb_err_t fmrb_hal_gpio_reset(fmrb_gpio_num_t gpio_num) {
    if (gpio_num < 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    hw_proxy_gpio_reset_params_t params = { .pin = gpio_num };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_GPIO_RESET, .params = &params };
    return hw_proxy_call(&req);
}

fmrb_err_t fmrb_hal_gpio_config(fmrb_gpio_num_t gpio_num,
                                 fmrb_gpio_mode_t mode,
                                 fmrb_gpio_pull_mode_t pull) {
    if (gpio_num < 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    hw_proxy_gpio_config_params_t params = { .pin = gpio_num, .mode = mode, .pull = pull };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_GPIO_CONFIG, .params = &params };
    return hw_proxy_call(&req);
}

fmrb_err_t fmrb_hal_gpio_set_level(fmrb_gpio_num_t gpio_num, uint32_t level) {
    if (gpio_num < 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    hw_proxy_gpio_write_params_t params = { .pin = gpio_num, .level = level };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_GPIO_WRITE, .params = &params };
    return hw_proxy_call(&req);
}

int32_t fmrb_hal_gpio_get_level(fmrb_gpio_num_t gpio_num) {
    if (gpio_num < 0) {
        return -1;
    }

    int32_t level = -1;
    hw_proxy_gpio_read_params_t params = { .pin = gpio_num, .out_level = &level };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_GPIO_READ, .params = &params };
    fmrb_err_t ret = hw_proxy_call(&req);
    if (ret != FMRB_OK) {
        return -1;
    }
    return level;
}

fmrb_err_t fmrb_hal_gpio_toggle(fmrb_gpio_num_t gpio_num) {
    int32_t current_level = fmrb_hal_gpio_get_level(gpio_num);
    if (current_level < 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    return fmrb_hal_gpio_set_level(gpio_num, current_level ? 0 : 1);
}

fmrb_err_t fmrb_hal_gpio_set_pull_mode(fmrb_gpio_num_t gpio_num, fmrb_gpio_pull_mode_t pull) {
    if (gpio_num < 0) {
        return FMRB_ERR_INVALID_PARAM;
    }

    hw_proxy_gpio_pull_params_t params = { .pin = gpio_num, .pull = pull };
    hw_proxy_request_t req = { .op = HW_PROXY_OP_GPIO_PULL, .params = &params };
    return hw_proxy_call(&req);
}
