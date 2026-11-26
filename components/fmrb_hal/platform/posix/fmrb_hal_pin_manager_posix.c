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
    if (s_pins[pin].usage == FMRB_PIN_SYSTEM_EXCLUSIVE) {
        return;
    }
    s_pins[pin].usage = FMRB_PIN_UNUSED;
    s_pins[pin].owner = NULL;
}

void fmrb_pin_manager_release_by_owner(void *owner)
{
    for (int i = 0; i < FMRB_PIN_MAX; i++) {
        if (s_pins[i].owner == owner && s_pins[i].usage != FMRB_PIN_SYSTEM_EXCLUSIVE) {
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

// The simulation has no GPIO class and registers no system pins; the same
// rule keeps the API whole (no pin is a button here).
bool fmrb_pin_manager_is_system_input(int pin)
{
    (void)pin;
    return false;
}

// No board, so no pin the system uses.
bool fmrb_pin_manager_is_gpio_reserved(int pin)
{
    (void)pin;
    return false;
}

fmrb_pin_status_t fmrb_pin_manager_get_app_status(int pin)
{
    return fmrb_pin_manager_get_status(pin);
}

// ---- UART (doc/app_uart) ----------------------------------------------------
//
// The 3-process simulation has a stand-in UART (fmrb_hal_uart_posix.c) and
// plays NARYA v3 for it: unit 2 is the apps', UART0 (the console) and UART1
// (the link to the drawing side) are the system's, and that board's pins for
// them, its USB lines, its boot pin and its memory bus are refused as system
// pins. This is for UART only: GPIO still sees no system pin here. The
// browser and the native program (FMRB_PLATFORM_HOSTED) have no UART at all.
#if defined(FMRB_PLATFORM_HOSTED)
#define UART_UNIT_COUNT 0
#define UART_APP_UNITS  0x0u
#define UART_SIM_RESERVED 0ULL
#else
#define UART_UNIT_COUNT 3
#define UART_APP_UNITS  0x4u
// 0 boot, 10-13 the link, 19-20 USB, 26-37 flash and PSRAM, 43-44 the console
#define UART_SIM_RESERVED \
    ((1ULL << 0) | (0xFULL << 10) | (3ULL << 19) | (0xFFFULL << 26) | (3ULL << 43))
#endif

fmrb_pin_uart_unit_t fmrb_pin_manager_uart_unit(int unit)
{
    if (unit < 0 || unit >= UART_UNIT_COUNT) {
        return FMRB_PIN_UART_UNIT_NONE;
    }
    return ((UART_APP_UNITS >> unit) & 1u) ? FMRB_PIN_UART_UNIT_APP : FMRB_PIN_UART_UNIT_SYSTEM;
}

uint32_t fmrb_pin_manager_uart_app_units(void)
{
    return UART_APP_UNITS;
}

static fmrb_pin_uart_verdict_t judge_uart_pin(int pin, bool tx, void *caller)
{
    if (pin < 0 || pin >= FMRB_PIN_MAX) {
        return FMRB_PIN_UART_NO_RANGE;
    }
    return fmrb_pin_uart_judge(s_pins[pin], ((UART_SIM_RESERVED >> pin) & 1ULL) != 0, false, tx,
                               caller);
}

fmrb_pin_uart_verdict_t fmrb_pin_manager_claim_uart(int tx, int rx, void *caller, int *which)
{
    fmrb_pin_uart_verdict_t v = FMRB_PIN_UART_OK;
    int bad = 0;
    if (tx >= 0 || tx < -1) {
        v = judge_uart_pin(tx, true, caller);
        bad = FMRB_PIN_UART_WHICH_TX;
    }
    if (v == FMRB_PIN_UART_OK && (rx >= 0 || rx < -1)) {
        v = judge_uart_pin(rx, false, caller);
        bad = FMRB_PIN_UART_WHICH_RX;
    }
    if (v != FMRB_PIN_UART_OK) {
        if (which) {
            *which = bad;
        }
        return v;
    }
    if (tx >= 0) {
        s_pins[tx].usage = FMRB_PIN_USER_UART;
        s_pins[tx].owner = caller;
    }
    if (rx >= 0) {
        s_pins[rx].usage = FMRB_PIN_USER_UART;
        s_pins[rx].owner = caller;
    }
    return FMRB_PIN_UART_OK;
}

fmrb_pin_gpio_verdict_t fmrb_pin_manager_claim_gpio(int pin, fmrb_pin_gpio_use_t use,
                                                    void *caller)
{
    if (pin < 0 || pin >= FMRB_PIN_MAX) {
        return FMRB_PIN_GPIO_NO_RANGE;
    }
    fmrb_pin_gpio_verdict_t v = fmrb_pin_gpio_judge(s_pins[pin], false, use, caller);
    if (v == FMRB_PIN_GPIO_OK && s_pins[pin].usage == FMRB_PIN_UNUSED) {
        s_pins[pin].usage = FMRB_PIN_USER_GPIO;
        s_pins[pin].owner = caller;
    }
    return v;
}
