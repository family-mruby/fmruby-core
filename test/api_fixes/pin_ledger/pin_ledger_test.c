/*
 * Host test of the pin table's GPIO rule (doc/api_fixes F4, C1): the ESP32
 * pin manager (components/fmrb_hal/platform/esp32/fmrb_hal_pin_manager_esp32.c)
 * built for one board (-DFMRB_HW_<BOARD>, the real fmrb_pin_assign.h) against
 * the stand-ins in stub/. Prints the board's buttons (the system pins an app
 * may read as an input), its other system pins and the pins GPIO must leave
 * alone (doc/api_fixes F6: the build-time table, which also holds pins the
 * table does not), then checks the verdicts.
 * test/api_fixes/run.rb builds it once per board and compares the lists.
 */
#include <stdio.h>
#include "fmrb_hal_pin_manager.h"

static int failures;

static void expect(const char *what, int pin, int got, int want)
{
    if (got != want) {
        printf("FAIL %s pin %d: got %d, want %d\n", what, pin, got, want);
        failures++;
    }
}

int main(void)
{
    int a = 1, b = 2;  /* two tasks */
    void *app_a = &a, *app_b = &b;

    fmrb_pin_manager_init();

    printf("inputs:");
    for (int pin = 0; pin < FMRB_PIN_MAX; pin++) {
        fmrb_pin_status_t st = fmrb_pin_manager_get_status(pin);
        if (st.usage == FMRB_PIN_SYSTEM_EXCLUSIVE && fmrb_pin_manager_is_system_input(pin)) {
            printf(" %d", pin);
        }
    }
    printf("\nsystem:");
    for (int pin = 0; pin < FMRB_PIN_MAX; pin++) {
        fmrb_pin_status_t st = fmrb_pin_manager_get_status(pin);
        if (st.usage == FMRB_PIN_SYSTEM_EXCLUSIVE && !fmrb_pin_manager_is_system_input(pin)) {
            printf(" %d", pin);
        }
    }
    printf("\nreserved:");
    for (int pin = 0; pin < FMRB_PIN_MAX; pin++) {
        if (fmrb_pin_manager_is_gpio_reserved(pin)) {
            printf(" %d", pin);
        }
    }
    printf("\n");

    /* The GPIO-only table: refused before the table is asked, reads go on,
     * the table is left as it was, and an app sees the pin as a system one. */
    int reserved_free = -1;
    for (int pin = 0; pin < FMRB_PIN_MAX; pin++) {
        fmrb_pin_status_t before = fmrb_pin_manager_get_status(pin);
        fmrb_pin_status_t app = fmrb_pin_manager_get_app_status(pin);
        if (!fmrb_pin_manager_is_gpio_reserved(pin)) {
            expect("app view of a pin outside the table", pin, app.usage, before.usage);
            continue;
        }
        expect("reserved pin is no button", pin, fmrb_pin_manager_is_system_input(pin), 0);
        expect("reserved read", pin, fmrb_pin_manager_claim_gpio(pin, FMRB_PIN_GPIO_READ, app_a),
               FMRB_PIN_GPIO_OK_SHARED);
        expect("reserved input", pin, fmrb_pin_manager_claim_gpio(pin, FMRB_PIN_GPIO_INPUT, app_a),
               FMRB_PIN_GPIO_NO_SYSTEM);
        expect("reserved output", pin, fmrb_pin_manager_claim_gpio(pin, FMRB_PIN_GPIO_OUTPUT, app_a),
               FMRB_PIN_GPIO_NO_SYSTEM);
        fmrb_pin_status_t after = fmrb_pin_manager_get_status(pin);
        expect("reserved pin's table entry left as it was", pin, after.usage, before.usage);
        expect("app view of a reserved pin", pin, app.usage, FMRB_PIN_SYSTEM_EXCLUSIVE);
        if (before.usage == FMRB_PIN_UNUSED && reserved_free < 0) reserved_free = pin;
    }
    expect("out of range is not reserved", FMRB_PIN_MAX,
           fmrb_pin_manager_is_gpio_reserved(FMRB_PIN_MAX), 0);
    expect("negative is not reserved", -1, fmrb_pin_manager_is_gpio_reserved(-1), 0);

    /* A reserved pin the table leaves free still goes to I2C as before; the
     * app view then shows the I2C, and GPIO still refuses it. */
    if (reserved_free >= 0) {
        int q = reserved_free;
        expect("i2c on a reserved pin", q, fmrb_pin_manager_acquire(q, FMRB_PIN_USER_I2C, app_a), FMRB_OK);
        expect("app view shows the i2c", q, fmrb_pin_manager_get_app_status(q).usage, FMRB_PIN_USER_I2C);
        expect("gpio on it, same app", q, fmrb_pin_manager_claim_gpio(q, FMRB_PIN_GPIO_INPUT, app_a),
               FMRB_PIN_GPIO_NO_SYSTEM);
        fmrb_pin_manager_release_by_owner(app_a);
        expect("released", q, fmrb_pin_manager_get_status(q).usage, FMRB_PIN_UNUSED);
        expect("app view after", q, fmrb_pin_manager_get_app_status(q).usage, FMRB_PIN_SYSTEM_EXCLUSIVE);
    }
    printf("reserved_free: %d\n", reserved_free);

    int free_pin = -1;
    for (int pin = 0; pin < FMRB_PIN_MAX; pin++) {
        fmrb_pin_status_t st = fmrb_pin_manager_get_status(pin);
        if (st.usage == FMRB_PIN_SYSTEM_EXCLUSIVE) {
            int button = fmrb_pin_manager_is_system_input(pin);
            expect("system read", pin, fmrb_pin_manager_claim_gpio(pin, FMRB_PIN_GPIO_READ, app_a),
                   FMRB_PIN_GPIO_OK_SHARED);
            expect("system input", pin, fmrb_pin_manager_claim_gpio(pin, FMRB_PIN_GPIO_INPUT, app_a),
                   button ? FMRB_PIN_GPIO_OK_SHARED : FMRB_PIN_GPIO_NO_SYSTEM);
            expect("system output", pin, fmrb_pin_manager_claim_gpio(pin, FMRB_PIN_GPIO_OUTPUT, app_a),
                   button ? FMRB_PIN_GPIO_NO_INPUT_ONLY : FMRB_PIN_GPIO_NO_SYSTEM);
            fmrb_pin_status_t after = fmrb_pin_manager_get_status(pin);
            expect("system pin left as it was", pin, after.usage, FMRB_PIN_SYSTEM_EXCLUSIVE);
            expect("system pin owner left as it was", pin, after.owner != NULL, 0);
        } else {
            expect("free pin is no button", pin, fmrb_pin_manager_is_system_input(pin), 0);
            if (free_pin < 0 && !fmrb_pin_manager_is_gpio_reserved(pin)) free_pin = pin;
        }
    }

    /* A free pin: the first to set it up owns it; the other app is refused. */
    int p = free_pin;
    expect("free read takes nothing", p, fmrb_pin_manager_claim_gpio(p, FMRB_PIN_GPIO_READ, app_a),
           FMRB_PIN_GPIO_OK_SHARED);
    expect("free still free", p, fmrb_pin_manager_get_status(p).usage, FMRB_PIN_UNUSED);
    expect("free input", p, fmrb_pin_manager_claim_gpio(p, FMRB_PIN_GPIO_INPUT, app_a), FMRB_PIN_GPIO_OK);
    expect("taken as GPIO", p, fmrb_pin_manager_get_status(p).usage, FMRB_PIN_USER_GPIO);
    expect("owner's output", p, fmrb_pin_manager_claim_gpio(p, FMRB_PIN_GPIO_OUTPUT, app_a), FMRB_PIN_GPIO_OK);
    expect("other app", p, fmrb_pin_manager_claim_gpio(p, FMRB_PIN_GPIO_INPUT, app_b), FMRB_PIN_GPIO_NO_IN_USE);
    expect("other app reads", p, fmrb_pin_manager_claim_gpio(p, FMRB_PIN_GPIO_READ, app_b),
           FMRB_PIN_GPIO_OK_SHARED);
    fmrb_pin_manager_release_by_owner(app_a);
    expect("released at exit", p, fmrb_pin_manager_get_status(p).usage, FMRB_PIN_UNUSED);
    expect("other app after", p, fmrb_pin_manager_claim_gpio(p, FMRB_PIN_GPIO_OUTPUT, app_b), FMRB_PIN_GPIO_OK);
    fmrb_pin_manager_release_by_owner(app_b);

    /* A pin held for I2C, even by the same app, is not a GPIO. */
    expect("i2c acquire", p, fmrb_pin_manager_acquire(p, FMRB_PIN_USER_I2C, app_a), FMRB_OK);
    expect("gpio on own i2c pin", p, fmrb_pin_manager_claim_gpio(p, FMRB_PIN_GPIO_INPUT, app_a),
           FMRB_PIN_GPIO_NO_IN_USE);
    fmrb_pin_manager_release_by_owner(app_a);

    expect("out of range", FMRB_PIN_MAX, fmrb_pin_manager_claim_gpio(FMRB_PIN_MAX, FMRB_PIN_GPIO_INPUT, app_a),
           FMRB_PIN_GPIO_NO_RANGE);
    expect("negative", -1, fmrb_pin_manager_claim_gpio(-1, FMRB_PIN_GPIO_READ, app_a), FMRB_PIN_GPIO_NO_RANGE);

    printf("checks failed: %d\n", failures);
    return failures ? 1 : 0;
}
