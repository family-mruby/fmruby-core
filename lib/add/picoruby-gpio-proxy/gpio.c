#include <stdint.h>
#include "gpio.h"
#include "fmrb_hal_gpio.h"

int GPIO_pin_num_from_char(const uint8_t *str)
{
    (void)str;
    return -1;
}

void GPIO_init(uint8_t pin)
{
    fmrb_hal_gpio_reset(pin);
}

void GPIO_set_dir(uint8_t pin, uint8_t dir)
{
    fmrb_gpio_mode_t mode;
    switch (dir) {
    case OUT:
        mode = FMRB_GPIO_MODE_OUTPUT;
        break;
    case IN:
    default:
        mode = FMRB_GPIO_MODE_INPUT;
        break;
    }
    fmrb_hal_gpio_config(pin, mode, FMRB_GPIO_PULL_NONE);
}

void GPIO_open_drain(uint8_t pin)
{
    (void)pin;
}

void GPIO_pull_up(uint8_t pin)
{
    fmrb_hal_gpio_set_pull_mode(pin, FMRB_GPIO_PULL_UP);
}

void GPIO_pull_down(uint8_t pin)
{
    fmrb_hal_gpio_set_pull_mode(pin, FMRB_GPIO_PULL_DOWN);
}

int GPIO_read(uint8_t pin)
{
    return fmrb_hal_gpio_get_level(pin);
}

void GPIO_write(uint8_t pin, uint8_t val)
{
    fmrb_hal_gpio_set_level(pin, val);
}

void GPIO_set_function(uint8_t pin, uint8_t function)
{
    (void)pin;
    (void)function;
}
