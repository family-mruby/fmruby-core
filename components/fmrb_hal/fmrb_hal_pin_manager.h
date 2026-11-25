#pragma once

#include "fmrb_hal.h"
#include <stdint.h>
#include <stdbool.h>

// sdkconfig.h must be pulled in explicitly: this header can be the first
// include of a translation unit, and the target branch below must not
// depend on include order (ESP_PLATFORM comes from the compiler command
// line, so it is always visible).
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define FMRB_PIN_MAX 55  // ESP32-P4: GPIO 0-54
#else
#define FMRB_PIN_MAX 49  // ESP32-S3: GPIO 0-48 (linux sim shares this table)
#endif

// Pin usage type
typedef enum {
    FMRB_PIN_UNUSED       = 0,
    FMRB_PIN_SYSTEM_EXCLUSIVE = 1,  // Absolutely reserved by system (USB, PSRAM, SPI, etc.)
    FMRB_PIN_USER_GPIO    = 2,  // Acquired by user app as GPIO
    FMRB_PIN_USER_I2C     = 3,  // Acquired by I2C init
    FMRB_PIN_USER_RMT     = 4,  // Acquired by RMT init
    FMRB_PIN_USER_SPI     = 5,  // Acquired by SPI
    FMRB_PIN_USER_PWM     = 6,  // Acquired by PWM
    FMRB_PIN_USER_UART    = 7,  // Acquired by UART
} fmrb_pin_usage_t;

typedef struct {
    fmrb_pin_usage_t usage;
    void *owner;   // task handle of the owner (NULL for system pins)
} fmrb_pin_status_t;

// Initialize pin manager (registers system-reserved pins)
void fmrb_pin_manager_init(void);

// Get status of a single pin
fmrb_pin_status_t fmrb_pin_manager_get_status(int pin);

// Acquire a pin for a specific usage
// Returns FMRB_OK if available, FMRB_ERR_BUSY if already in use
fmrb_err_t fmrb_pin_manager_acquire(int pin, fmrb_pin_usage_t usage, void *owner);

// Release a pin
void fmrb_pin_manager_release(int pin);

// Release all pins owned by a specific task
void fmrb_pin_manager_release_by_owner(void *owner);

// Check if a pin is available for user use
bool fmrb_pin_manager_is_available(int pin);

// ---- What an app may do with a pin through the GPIO class ----------------
//
// A system pin is refused, except the board's own buttons (FMRB_PIN_BUTTON_*
// in fmrb_pin_assign.h), which an app may set up as an input and read while
// the system keeps them. Reading a level never changes a pin, so it is
// allowed on any pin.

// What the app is about to do.
typedef enum {
    FMRB_PIN_GPIO_READ   = 0,  // read the level
    FMRB_PIN_GPIO_INPUT  = 1,  // input direction, pull-up / pull-down
    FMRB_PIN_GPIO_OUTPUT = 2,  // output direction, write, open drain, alternate function
} fmrb_pin_gpio_use_t;

// The answer. Only the first two let the app go on.
typedef enum {
    FMRB_PIN_GPIO_OK            = 0,  // the caller owns the pin (taken now or before)
    FMRB_PIN_GPIO_OK_SHARED     = 1,  // allowed without owning it: a read, or input on a button
    FMRB_PIN_GPIO_NO_RANGE      = 2,  // no such pin on this chip
    FMRB_PIN_GPIO_NO_SYSTEM     = 3,  // a pin the system uses
    FMRB_PIN_GPIO_NO_INPUT_ONLY = 4,  // a button, asked for output
    FMRB_PIN_GPIO_NO_IN_USE     = 5,  // held by another app, or for another use (I2C ...)
} fmrb_pin_gpio_verdict_t;

static inline bool fmrb_pin_gpio_granted(fmrb_pin_gpio_verdict_t v)
{
    return v == FMRB_PIN_GPIO_OK || v == FMRB_PIN_GPIO_OK_SHARED;
}

// The rule on one pin's status, without touching the table (the unit the
// host test checks). system_input: the pin is one of the board's buttons.
static inline fmrb_pin_gpio_verdict_t
fmrb_pin_gpio_judge(fmrb_pin_status_t st, bool system_input,
                    fmrb_pin_gpio_use_t use, const void *caller)
{
    if (use == FMRB_PIN_GPIO_READ) {
        return FMRB_PIN_GPIO_OK_SHARED;
    }
    switch (st.usage) {
    case FMRB_PIN_UNUSED:
        return FMRB_PIN_GPIO_OK;
    case FMRB_PIN_SYSTEM_EXCLUSIVE:
        if (!system_input) {
            return FMRB_PIN_GPIO_NO_SYSTEM;
        }
        return use == FMRB_PIN_GPIO_INPUT ? FMRB_PIN_GPIO_OK_SHARED
                                          : FMRB_PIN_GPIO_NO_INPUT_ONLY;
    case FMRB_PIN_USER_GPIO:
        return st.owner == caller ? FMRB_PIN_GPIO_OK : FMRB_PIN_GPIO_NO_IN_USE;
    default:
        return FMRB_PIN_GPIO_NO_IN_USE;
    }
}

// Whether the pin is one of the board's buttons: a system pin an app may
// read as an input. Decided from fmrb_pin_assign.h at build time (no table).
bool fmrb_pin_manager_is_system_input(int pin);

// Whether the pin is one the system uses that an app must not set up as a
// GPIO: the board's display, SD, radio, audio, console, system I2C and the
// like (fmrb_pin_assign.h), whether or not the table holds it. A build-time
// constant per board; the buttons are never in it. Only the GPIO class looks
// at it -- I2C, RMT and the table itself do not.
bool fmrb_pin_manager_is_gpio_reserved(int pin);

// Judge a GPIO use of the pin for caller and, when the pin is free and the
// use sets it up (INPUT, OUTPUT), take it for caller as FMRB_PIN_USER_GPIO.
// A pin fmrb_pin_manager_is_gpio_reserved() names is refused as a system pin
// before the table is asked (reading is still allowed). A refusal leaves the
// table as it was.
fmrb_pin_gpio_verdict_t fmrb_pin_manager_claim_gpio(int pin, fmrb_pin_gpio_use_t use,
                                                    void *caller);

// How the pin looks to an app (FmrbHw.pin_status): the table's status, except
// that a free pin fmrb_pin_manager_is_gpio_reserved() names reads as
// FMRB_PIN_SYSTEM_EXCLUSIVE. Out of range is FMRB_PIN_UNUSED, as in
// fmrb_pin_manager_get_status().
fmrb_pin_status_t fmrb_pin_manager_get_app_status(int pin);

#ifdef __cplusplus
}
#endif
