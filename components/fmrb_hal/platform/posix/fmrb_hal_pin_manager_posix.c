#include "fmrb_hal_pin_manager.h"
#include <string.h>

static fmrb_pin_status_t s_pins[FMRB_PIN_MAX];

void fmrb_pin_manager_init(void)
{
    memset(s_pins, 0, sizeof(s_pins));
}

fmrb_pin_status_t fmrb_pin_manager_get_status(int pin)
{
    fmrb_pin_status_t empty = { .usage = FMRB_PIN_UNUSED, .owner = NULL };
    if (pin < 0 || pin >= FMRB_PIN_MAX) {
        return empty;
    }
    return s_pins[pin];
}

fmrb_err_t fmrb_pin_manager_acquire(int pin, fmrb_pin_usage_t usage, void *owner)
{
    if (pin < 0 || pin >= FMRB_PIN_MAX) {
        return FMRB_ERR_INVALID_PARAM;
    }
    if (s_pins[pin].usage != FMRB_PIN_UNUSED) {
        if (s_pins[pin].owner == owner && s_pins[pin].usage == usage) {
            return FMRB_OK;
        }
        return FMRB_ERR_BUSY;
    }
    s_pins[pin].usage = usage;
    s_pins[pin].owner = owner;
    return FMRB_OK;
}

void fmrb_pin_manager_release(int pin)
{
    if (pin < 0 || pin >= FMRB_PIN_MAX) {
        return;
    }
    if (s_pins[pin].usage == FMRB_PIN_SYSTEM) {
        return;
    }
    s_pins[pin].usage = FMRB_PIN_UNUSED;
    s_pins[pin].owner = NULL;
}

void fmrb_pin_manager_release_by_owner(void *owner)
{
    for (int i = 0; i < FMRB_PIN_MAX; i++) {
        if (s_pins[i].owner == owner && s_pins[i].usage != FMRB_PIN_SYSTEM) {
            s_pins[i].usage = FMRB_PIN_UNUSED;
            s_pins[i].owner = NULL;
        }
    }
}

bool fmrb_pin_manager_is_available(int pin)
{
    if (pin < 0 || pin >= FMRB_PIN_MAX) {
        return false;
    }
    return s_pins[pin].usage == FMRB_PIN_UNUSED;
}
