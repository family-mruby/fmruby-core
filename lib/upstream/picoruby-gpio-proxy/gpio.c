#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio.h"
#include "fmrb_hal_gpio.h"
#include "fmrb_hal_pin_manager.h"

// The gem's codes are the pin manager's (fmrb_hal_pin_manager.h).
_Static_assert(GPIO_USE_READ == FMRB_PIN_GPIO_READ &&
               GPIO_USE_INPUT == FMRB_PIN_GPIO_INPUT &&
               GPIO_USE_OUTPUT == FMRB_PIN_GPIO_OUTPUT, "GPIO uses");
_Static_assert(GPIO_CLAIM_OK == FMRB_PIN_GPIO_OK &&
               GPIO_CLAIM_OK_SHARED == FMRB_PIN_GPIO_OK_SHARED &&
               GPIO_CLAIM_NO_RANGE == FMRB_PIN_GPIO_NO_RANGE &&
               GPIO_CLAIM_NO_SYSTEM == FMRB_PIN_GPIO_NO_SYSTEM &&
               GPIO_CLAIM_INPUT_ONLY == FMRB_PIN_GPIO_NO_INPUT_ONLY &&
               GPIO_CLAIM_IN_USE == FMRB_PIN_GPIO_NO_IN_USE, "GPIO verdicts");

int GPIO_pin_num_from_char(const uint8_t *str)
{
    (void)str;
    return -1;
}

// Reading never changes a pin, and a pin this task already holds as GPIO
// stays its own until the app ends, so neither needs the hw_proxy round
// trip; everything else is judged (and taken) on the proxy task.
int GPIO_claim(int pin, int use, int reset)
{
    if (pin < 0 || pin >= FMRB_PIN_MAX) {
        return GPIO_CLAIM_NO_RANGE;
    }
    if (use == GPIO_USE_READ) {
        return GPIO_CLAIM_OK_SHARED;
    }
    if (!reset) {
        fmrb_pin_status_t st = fmrb_pin_manager_get_status(pin);
        if (st.usage == FMRB_PIN_USER_GPIO && st.owner == (void *)xTaskGetCurrentTaskHandle()) {
            return GPIO_CLAIM_OK;
        }
    }
    int v = fmrb_hal_gpio_claim(pin, use, reset != 0);
    return v < 0 ? GPIO_CLAIM_FAILED : v;
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
