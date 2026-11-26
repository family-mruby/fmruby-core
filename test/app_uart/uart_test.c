/*
 * Host test of an app's UART (doc/app_uart U1), the parts that are pure C:
 *
 *   - the receive ring (main/app/fmrb_uart_ring.h): order, wrapping, a full
 *     ring keeps what it had and drops the newest, putting a byte back, the
 *     end of a line;
 *   - the pin table's UART rule (fmrb_hal_pin_manager_*.c, built for one
 *     board with -DFMRB_HW_<BOARD> and its real fmrb_pin_assign.h, or the
 *     POSIX one for the sim): which units an app may open, system pins,
 *     a chip's interrupt line as TX, another app's pins, both pins or none.
 *
 * The board's pins come from the command line (test/app_uart/run.rb):
 * TX_PIN / RX_PIN free pins an app may use (RX_PIN may be -1), SYS_PIN a
 * system pin, IRQ_PIN a chip's interrupt line (-1 when the board has none).
 * Prints the units ("app:" and "system:") for run.rb to compare.
 */
#include <stdio.h>
#include <string.h>
#include "fmrb_hal_pin_manager.h"
#include "fmrb_uart_ring.h"

static int failures;
static int checks;

static void expect(const char *what, long got, long want)
{
    checks++;
    if (got != want) {
        printf("FAIL %s: got %ld, want %ld\n", what, got, want);
        failures++;
    }
}

static void ring_tests(void)
{
    uint8_t buf[8];
    uint8_t out[16];
    fmrb_uart_ring_t r;

    expect("size 8 ok", fmrb_uart_ring_size_ok(8), 1);
    expect("size 12 not", fmrb_uart_ring_size_ok(12), 0);
    expect("size 0 not", fmrb_uart_ring_size_ok(0), 0);

    fmrb_uart_ring_init(&r, buf, sizeof(buf));
    expect("empty used", fmrb_uart_ring_used(&r), 0);
    expect("push 5", fmrb_uart_ring_push(&r, (const uint8_t *)"abcde", 5), 5);
    expect("find none", fmrb_uart_ring_find(&r, '\n'), 0);
    fmrb_uart_ring_peek(&r, out, 3);
    expect("peek order", memcmp(out, "abc", 3), 0);
    expect("peek takes nothing", fmrb_uart_ring_used(&r), 5);
    fmrb_uart_ring_drop(&r, 3);
    expect("after drop", fmrb_uart_ring_used(&r), 2);

    // Wraps: 2 left (de) + 6 more = a full ring of 8; the 7th is dropped.
    expect("push wraps to full", fmrb_uart_ring_push(&r, (const uint8_t *)"f\nhijkl", 7), 6);
    expect("full", fmrb_uart_ring_free(&r), 0);
    fmrb_uart_ring_peek(&r, out, 8);
    expect("wrapped order", memcmp(out, "def\nhijk", 8), 0);
    expect("line end", fmrb_uart_ring_find(&r, '\n'), 4);
    expect("full drops all", fmrb_uart_ring_push(&r, (const uint8_t *)"z", 1), 0);
    expect("unshift on full", fmrb_uart_ring_unshift(&r, 'Q'), 0);

    fmrb_uart_ring_drop(&r, 4);
    expect("unshift", fmrb_uart_ring_unshift(&r, 'Q'), 1);
    fmrb_uart_ring_peek(&r, out, fmrb_uart_ring_used(&r));
    expect("unshift in front", memcmp(out, "Qhijk", 5), 0);

    fmrb_uart_ring_clear(&r);
    expect("cleared", fmrb_uart_ring_used(&r), 0);
    // Unshift on an empty ring whose head is at 0: the counters run free.
    fmrb_uart_ring_init(&r, buf, sizeof(buf));
    expect("unshift empty", fmrb_uart_ring_unshift(&r, 0x41), 1);
    fmrb_uart_ring_peek(&r, out, 1);
    expect("unshift empty reads back", out[0], 0x41);
    expect("unshift empty count", fmrb_uart_ring_used(&r), 1);
}

static void unit_tests(void)
{
    printf("app:");
    for (int u = 0; u < 8; u++) {
        if (fmrb_pin_manager_uart_unit(u) == FMRB_PIN_UART_UNIT_APP) {
            printf(" %d", u);
            expect("mask has the app unit", (fmrb_pin_manager_uart_app_units() >> u) & 1u, 1);
        }
    }
    printf("\nsystem:");
    for (int u = 0; u < 8; u++) {
        if (fmrb_pin_manager_uart_unit(u) == FMRB_PIN_UART_UNIT_SYSTEM) {
            printf(" %d", u);
        }
    }
    printf("\n");
    expect("negative unit", fmrb_pin_manager_uart_unit(-1), FMRB_PIN_UART_UNIT_NONE);
}

static void claim_tests(void)
{
    int a = 1, b = 2;
    void *app_a = &a, *app_b = &b;
    int which = 0;

    fmrb_pin_manager_init();

    // Free pins: taken for app A, and again for app A (reopened).
    expect("free pins", fmrb_pin_manager_claim_uart(TX_PIN, RX_PIN, app_a, &which), FMRB_PIN_UART_OK);
    expect("tx owned", fmrb_pin_manager_get_status(TX_PIN).usage, FMRB_PIN_USER_UART);
    expect("same app again", fmrb_pin_manager_claim_uart(TX_PIN, RX_PIN, app_a, &which), FMRB_PIN_UART_OK);

    // Another app: refused on TX, nothing changes.
    which = 0;
    expect("other app", fmrb_pin_manager_claim_uart(TX_PIN, -1, app_b, &which), FMRB_PIN_UART_NO_IN_USE);
    expect("other app which", which, FMRB_PIN_UART_WHICH_TX);
    expect("still app A's", fmrb_pin_manager_get_status(TX_PIN).owner == app_a, 1);
    if (RX_PIN >= 0) {
        which = 0;
        expect("other app on rx", fmrb_pin_manager_claim_uart(-1, RX_PIN, app_b, &which),
               FMRB_PIN_UART_NO_IN_USE);
        expect("other app on rx which", which, FMRB_PIN_UART_WHICH_RX);
    }
    fmrb_pin_manager_release(TX_PIN);
    if (RX_PIN >= 0) {
        fmrb_pin_manager_release(RX_PIN);
    }

    // Both or neither: a refused RX leaves the TX pin free.
    which = 0;
    expect("system rx", fmrb_pin_manager_claim_uart(TX_PIN, SYS_PIN, app_a, &which), FMRB_PIN_UART_NO_SYSTEM);
    expect("system rx which", which, FMRB_PIN_UART_WHICH_RX);
    expect("tx left free", fmrb_pin_manager_get_status(TX_PIN).usage, FMRB_PIN_UNUSED);
    expect("system tx", fmrb_pin_manager_claim_uart(SYS_PIN, -1, app_a, &which), FMRB_PIN_UART_NO_SYSTEM);

    expect("out of range", fmrb_pin_manager_claim_uart(FMRB_PIN_MAX, -1, app_a, &which), FMRB_PIN_UART_NO_RANGE);

    // A pin GPIO holds, or one held for UART with no owner (the MIDI output).
    expect("gpio holds it", fmrb_pin_manager_acquire(TX_PIN, FMRB_PIN_USER_GPIO, app_a), FMRB_OK);
    expect("not a uart pin", fmrb_pin_manager_claim_uart(TX_PIN, -1, app_a, &which), FMRB_PIN_UART_NO_IN_USE);
    fmrb_pin_manager_release(TX_PIN);
    expect("midi holds it", fmrb_pin_manager_acquire(TX_PIN, FMRB_PIN_USER_UART, NULL), FMRB_OK);
    expect("midi's pin", fmrb_pin_manager_claim_uart(TX_PIN, -1, app_a, &which), FMRB_PIN_UART_NO_IN_USE);
    fmrb_pin_manager_release(TX_PIN);

    // A chip's interrupt line: RX only.
    if (IRQ_PIN >= 0) {
        expect("irq as tx", fmrb_pin_manager_claim_uart(IRQ_PIN, -1, app_a, &which),
               FMRB_PIN_UART_NO_INPUT_ONLY);
        expect("irq as rx", fmrb_pin_manager_claim_uart(TX_PIN, IRQ_PIN, app_a, &which), FMRB_PIN_UART_OK);
        fmrb_pin_manager_release_by_owner(app_a);
        expect("released", fmrb_pin_manager_get_status(IRQ_PIN).usage, FMRB_PIN_UNUSED);
    }
}

int main(void)
{
    ring_tests();
    unit_tests();
    claim_tests();
    printf("uart: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
