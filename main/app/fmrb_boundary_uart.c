/*
 * The app boundary's FmrbUart flat functions (doc/app_uart): the C bodies
 * behind fmrb_gen/fmrb_boundary.h, generated from sig/uart.rbs. Every
 * engine's glue is generated and calls these; the Ruby class UART
 * (lib/gems/picoruby-fmrb-app/mrblib/fmrb-uart.rb) is a thin wrapper over them.
 *
 * One slot per UART unit holds who opened it (the app's task), the HAL's
 * handle, its pins and a receive ring in PSRAM. Every call checks that the
 * caller is the one that opened the unit. The bytes the driver received are
 * moved into the ring when the app calls (nothing runs in between); a byte
 * that does not fit the ring is dropped and counted. Reads never wait;
 * write waits until the bytes are handed to the driver.
 *
 * A read whose buffer is too small for its answer takes nothing, gives the
 * first out_size bytes and returns the whole length; the next read with room
 * for that length takes exactly those bytes (as the glue asks again), so the
 * two calls agree although more may have arrived in between.
 *
 * The browser and the native program have no UART: open answers
 * FMRB_UART_UNSUPPORTED there and unit_mask 0. The 3-process simulation has
 * the HAL's stand-in (a loopback, or FIFOs; fmrb_hal_uart.h).
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "fmrb_gen/fmrb_boundary.h"
#include "fmrb_app.h"
#include "fmrb_app_uart.h"
#include "fmrb_attr.h"
#include "fmrb_err.h"
#include "fmrb_hal_pin_manager.h"
#include "fmrb_hal_uart.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_rtos.h"
#include "fmrb_uart_ring.h"
#if !defined(FMRB_PLATFORM_HOSTED)
#include "fmrb_midi_serial.h"
#include "fmrb_pin_assign.h"
#endif

#define ERR_FAILED FMRB_BOUNDARY_ERR_FAILED
#define ERR_ARG    FMRB_BOUNDARY_ERR_ARG
#define NONE       FMRB_BOUNDARY_NONE

static const char *TAG = "uart";

// The most units any chip has (the ESP32-P4's 5 HP UARTs).
#define UART_SLOTS 5
// The driver's own receive buffer (internal RAM, while open).
#define DRIVER_RX_SIZE 1024
// The unit the serial MIDI output uses (fmrb_midi_serial.c).
#define MIDI_UNIT 2
// The longest flush waits for the wire.
#define FLUSH_TIMEOUT_MS 1000

typedef struct {
    void *owner;              // the app's task; NULL when the unit is free
    fmrb_uart_handle_t hal;
    fmrb_uart_ring_t ring;    // the buffer is fmrb_sys_malloc'ed (the PSRAM system pool)
    uint32_t dropped;         // bytes the ring had no room for
    uint32_t pending;         // the length a read gave without taking (see the top)
    int16_t tx;
    int16_t rx;
} uart_slot_t;

// In PSRAM, as the pin table is: read and written only from app tasks.
FMRB_EXT_RAM_BSS_ATTR static uart_slot_t s_slots[UART_SLOTS];
// Made the first time an app opens a UART (fmrb_app_mutex_once).
FMRB_EXT_RAM_BSS_ATTR static fmrb_semaphore_t s_lock;

static void *caller(void)
{
    return (void *)fmrb_task_get_current();
}

// The caller's open slot, or NULL.
static uart_slot_t *own_slot(int32_t unit)
{
    if (unit < 0 || unit >= UART_SLOTS) {
        return NULL;
    }
    uart_slot_t *s = &s_slots[unit];
    if (s->owner == NULL || s->owner != caller()) {
        return NULL;
    }
    return s;
}

// What the driver received, into the ring; a byte with no room is dropped.
static void pump(uart_slot_t *s)
{
    uint8_t tmp[128];
    for (int round = 0; round < 64; round++) {
        size_t avail = 0;
        if (fmrb_hal_uart_rx_available(s->hal, &avail) != FMRB_OK || avail == 0) {
            break;
        }
        size_t got = 0;
        fmrb_err_t err = fmrb_hal_uart_read(s->hal, tmp, avail < sizeof(tmp) ? avail : sizeof(tmp), &got);
        if (err != FMRB_OK || got == 0) {
            break;
        }
        uint32_t in = fmrb_uart_ring_push(&s->ring, tmp, (uint32_t)got);
        s->dropped += (uint32_t)got - in;
    }
}

static void release_pin(int pin, void *owner)
{
    if (pin < 0) {
        return;
    }
    fmrb_pin_status_t st = fmrb_pin_manager_get_status(pin);
    if (st.usage == FMRB_PIN_USER_UART && st.owner == owner) {
        fmrb_pin_manager_release(pin);
    }
}

// Under s_lock (or for an app that is gone). The pins keep_a and keep_b
// stay taken (the same app opening the unit again on them).
static void close_slot(int unit, uart_slot_t *s, int keep_a, int keep_b)
{
    void *owner = s->owner;
    if (s->hal) {
        fmrb_hal_uart_close(s->hal);
    }
    if (s->ring.buf) {
        fmrb_sys_free(s->ring.buf);
    }
    if (s->tx != keep_a && s->tx != keep_b) {
        release_pin(s->tx, owner);
    }
    if (s->rx != keep_a && s->rx != keep_b) {
        release_pin(s->rx, owner);
    }
    memset(s, 0, sizeof(*s));
    FMRB_LOGI(TAG, "UART%d closed", unit);
}

#if !defined(FMRB_PLATFORM_HOSTED)
// The serial MIDI output keeps its unit and its TX pin while an app that
// asked for it runs; the last one ending gives them back
// (fmrb_midi_serial_release_owner; doc/app_uart plan 7.3).
static bool midi_holds_unit(int32_t unit)
{
    if (unit != MIDI_UNIT || !fmrb_midi_serial_is_open()) {
        return false;
    }
#if defined(CONFIG_IDF_TARGET_LINUX)
    return true;
#else
    return (int)FMRB_PIN_I2C2_SDA >= 0;
#endif
}

static int32_t pin_reason(fmrb_pin_uart_verdict_t v, int pin)
{
    switch (v) {
    case FMRB_PIN_UART_NO_RANGE:
        return FMRB_UART_BAD_PIN;
    case FMRB_PIN_UART_NO_SYSTEM:
        return FMRB_UART_SYSTEM_PIN;
    case FMRB_PIN_UART_NO_INPUT_ONLY:
        return FMRB_UART_INPUT_ONLY;
    default: {
        // The MIDI output takes its pin as a UART pin with no owner.
        fmrb_pin_status_t st = fmrb_pin_manager_get_status(pin);
        return st.usage == FMRB_PIN_USER_UART && st.owner == NULL ? FMRB_UART_MIDI : FMRB_UART_PIN_IN_USE;
    }
    }
}

// The part of open that needs s_lock (the arguments are checked).
static int32_t open_locked(int32_t unit, int32_t txd_pin, int32_t rxd_pin, int32_t baudrate, int32_t data_bits,
                           int32_t stop_bits, int32_t parity, int32_t rx_buffer_size)
{
    void *me = caller();
    uart_slot_t *s = &s_slots[unit];
    // The same app opening the unit again: the one it had is closed once the
    // new one passes the checks (a refused reopen leaves it open). Its pins
    // are the app's own, so the pin table lets them through.
    bool reopen = s->owner == me;
    if (s->owner != NULL && !reopen) {
        return FMRB_UART_UNIT_IN_USE;
    }
    if (!reopen && midi_holds_unit(unit)) {
        return FMRB_UART_MIDI;
    }
    int which = 0;
    fmrb_pin_uart_verdict_t v = fmrb_pin_manager_claim_uart((int)txd_pin, (int)rxd_pin, me, &which);
    if (v != FMRB_PIN_UART_OK) {
        return pin_reason(v, which == FMRB_PIN_UART_WHICH_TX ? (int)txd_pin : (int)rxd_pin) + 256 * which;
    }
    if (reopen) {
        close_slot((int)unit, s, (int)txd_pin, (int)rxd_pin);
    }

    uint8_t *buf = (uint8_t *)fmrb_sys_malloc((size_t)rx_buffer_size);
    fmrb_uart_config_t cfg = {
        .device_path = NULL,
        .uart_num = (int)unit,
        .tx_pin = (int)txd_pin,
        .rx_pin = (int)rxd_pin,
        .baud_rate = (uint32_t)baudrate,
        .timeout_ms = 0,
        .data_bits = (uint8_t)data_bits,
        .stop_bits = (uint8_t)stop_bits,
        .parity = (uint8_t)parity,
        .rx_buffer_size = DRIVER_RX_SIZE,
    };
    fmrb_uart_handle_t hal = NULL;
    if (buf == NULL || fmrb_hal_uart_open(&cfg, &hal) != FMRB_OK) {
        if (buf) {
            fmrb_sys_free(buf);
        }
        release_pin((int)txd_pin, me);
        release_pin((int)rxd_pin, me);
        return FMRB_UART_FAILED;
    }
    memset(s, 0, sizeof(*s));
    s->owner = me;
    s->hal = hal;
    fmrb_uart_ring_init(&s->ring, buf, (uint32_t)rx_buffer_size);
    s->tx = (int16_t)txd_pin;
    s->rx = (int16_t)rxd_pin;
    return FMRB_UART_OPENED;
}
#endif

int32_t fmrb_bnd_uart_open(int32_t unit, int32_t txd_pin, int32_t rxd_pin, int32_t baudrate, int32_t data_bits,
                           int32_t stop_bits, int32_t parity, int32_t rx_buffer_size)
{
#if defined(FMRB_PLATFORM_HOSTED)
    (void)unit; (void)txd_pin; (void)rxd_pin; (void)baudrate; (void)data_bits; (void)stop_bits;
    (void)parity; (void)rx_buffer_size;
    return FMRB_UART_UNSUPPORTED;
#else
    fmrb_pin_uart_unit_t kind = fmrb_pin_manager_uart_unit((int)unit);
    if (kind == FMRB_PIN_UART_UNIT_NONE || unit >= UART_SLOTS) {
        return FMRB_UART_NO_UNIT;
    }
    if (txd_pin < -1 || txd_pin >= FMRB_PIN_MAX) {
        return FMRB_UART_BAD_PIN + 256 * FMRB_PIN_UART_WHICH_TX;
    }
    if (rxd_pin < -1 || rxd_pin >= FMRB_PIN_MAX) {
        return FMRB_UART_BAD_PIN + 256 * FMRB_PIN_UART_WHICH_RX;
    }
    if (txd_pin < 0 && rxd_pin < 0) {
        return FMRB_UART_NO_PINS;
    }
    if (txd_pin == rxd_pin) {
        return FMRB_UART_SAME_PIN;
    }
    if (baudrate < FMRB_UART_BAUD_MIN || baudrate > FMRB_UART_BAUD_MAX) {
        return FMRB_UART_BAD_BAUD;
    }
    if (data_bits < 5 || data_bits > 8 || (stop_bits != 1 && stop_bits != 2) ||
        parity < FMRB_UART_PARITY_NONE || parity > FMRB_UART_PARITY_ODD) {
        return FMRB_UART_BAD_FORMAT;
    }
    if (rx_buffer_size < FMRB_UART_RING_MIN || rx_buffer_size > FMRB_UART_RING_MAX ||
        !fmrb_uart_ring_size_ok((uint32_t)rx_buffer_size)) {
        return FMRB_UART_BAD_BUFFER;
    }
    if (kind == FMRB_PIN_UART_UNIT_SYSTEM) {
        return FMRB_UART_SYSTEM_UNIT;
    }
    if (fmrb_app_mutex_once(&s_lock) == NULL) {
        return FMRB_UART_FAILED;
    }

    fmrb_semaphore_take(s_lock, FMRB_TICK_MAX);
    int32_t answer = open_locked(unit, txd_pin, rxd_pin, baudrate, data_bits, stop_bits, parity, rx_buffer_size);
    fmrb_semaphore_give(s_lock);
    if (answer == FMRB_UART_OPENED) {
        FMRB_LOGI(TAG, "UART%d open (tx=%d rx=%d %d baud, ring %d)", (int)unit, (int)txd_pin, (int)rxd_pin,
                  (int)baudrate, (int)rx_buffer_size);
    }
    return answer;
#endif
}

int32_t fmrb_bnd_uart_close(int32_t unit)
{
    uart_slot_t *s = own_slot(unit);
    if (!s || s_lock == NULL) {
        return 0;
    }
    fmrb_semaphore_take(s_lock, FMRB_TICK_MAX);
    close_slot((int)unit, s, -1, -1);
    fmrb_semaphore_give(s_lock);
    return 1;
}

int32_t fmrb_bnd_uart_read(int32_t unit, int32_t max, uint8_t *out, int32_t out_size)
{
    uart_slot_t *s = own_slot(unit);
    if (!s) {
        return ERR_FAILED;
    }
    if (max < 0 || out_size < 0 || (out_size > 0 && !out)) {
        return ERR_ARG;
    }
    pump(s);
    uint32_t used = fmrb_uart_ring_used(&s->ring);
    uint32_t n = (uint32_t)max < used ? (uint32_t)max : used;
    if (s->pending != 0 && s->pending <= used && s->pending <= (uint32_t)max && s->pending <= (uint32_t)out_size) {
        n = s->pending;
    }
    s->pending = 0;
    if (n <= (uint32_t)out_size) {
        if (n > 0) {
            fmrb_uart_ring_peek(&s->ring, out, n);
            fmrb_uart_ring_drop(&s->ring, n);
        }
    } else {
        if (out_size > 0) {
            fmrb_uart_ring_peek(&s->ring, out, (uint32_t)out_size);
        }
        s->pending = n;
    }
    return (int32_t)n;
}

// The bytes up to and including the first "\n" (where it is does not move
// as more arrive, so the glue's second call takes the same line).
int32_t fmrb_bnd_uart_read_line(int32_t unit, uint8_t *out, int32_t out_size)
{
    uart_slot_t *s = own_slot(unit);
    if (!s) {
        return ERR_FAILED;
    }
    if (out_size < 0 || (out_size > 0 && !out)) {
        return ERR_ARG;
    }
    pump(s);
    s->pending = 0;
    uint32_t n = fmrb_uart_ring_find(&s->ring, (uint8_t)'\n');
    if (n == 0) {
        return NONE;
    }
    if (n <= (uint32_t)out_size) {
        fmrb_uart_ring_peek(&s->ring, out, n);
        fmrb_uart_ring_drop(&s->ring, n);
    } else if (out_size > 0) {
        fmrb_uart_ring_peek(&s->ring, out, (uint32_t)out_size);
    }
    return (int32_t)n;
}

int32_t fmrb_bnd_uart_write(int32_t unit, const uint8_t *data, int32_t data_len)
{
    uart_slot_t *s = own_slot(unit);
    if (!s) {
        return ERR_FAILED;
    }
    if (data_len < 0 || data_len > FMRB_UART_WRITE_MAX || (data_len > 0 && !data)) {
        return ERR_ARG;
    }
    int32_t done = 0;
    int idle_ms = 0;
    while (done < data_len) {
        size_t put = 0;
        if (fmrb_hal_uart_write(s->hal, data + done, (size_t)(data_len - done), &put) != FMRB_OK) {
            break;
        }
        if (put == 0) {
            // The simulation's pipe or FIFO is full: wait a little, then give up.
            if (++idle_ms > 1000) {
                break;
            }
            fmrb_task_delay_ms(1);
            continue;
        }
        done += (int32_t)put;
    }
    return done;
}

int32_t fmrb_bnd_uart_bytes_available(int32_t unit)
{
    uart_slot_t *s = own_slot(unit);
    if (!s) {
        return ERR_FAILED;
    }
    pump(s);
    return (int32_t)fmrb_uart_ring_used(&s->ring);
}

int32_t fmrb_bnd_uart_unget(int32_t unit, int32_t byte)
{
    uart_slot_t *s = own_slot(unit);
    if (!s) {
        return ERR_FAILED;
    }
    s->pending = 0;
    return fmrb_uart_ring_unshift(&s->ring, (uint8_t)(byte & 0xff)) ? 1 : 0;
}

int32_t fmrb_bnd_uart_flush(int32_t unit)
{
    uart_slot_t *s = own_slot(unit);
    if (!s) {
        return ERR_FAILED;
    }
    return fmrb_hal_uart_wait_tx(s->hal, FLUSH_TIMEOUT_MS) == FMRB_OK ? 1 : 0;
}

int32_t fmrb_bnd_uart_clear_rx(int32_t unit)
{
    uart_slot_t *s = own_slot(unit);
    if (!s) {
        return ERR_FAILED;
    }
    fmrb_hal_uart_flush_rx(s->hal);
    fmrb_uart_ring_clear(&s->ring);
    s->pending = 0;
    return 0;
}

// The driver cannot take back what it was given to send: as upstream's
// ESP32 port, nothing to do but check the caller.
int32_t fmrb_bnd_uart_clear_tx(int32_t unit)
{
    return own_slot(unit) ? 0 : ERR_FAILED;
}

int32_t fmrb_bnd_uart_send_break(int32_t unit, int32_t ms)
{
    uart_slot_t *s = own_slot(unit);
    if (!s) {
        return ERR_FAILED;
    }
    if (ms < 0 || ms > FMRB_UART_BREAK_MAX_MS) {
        return ERR_ARG;
    }
    return fmrb_hal_uart_send_break(s->hal, (uint32_t)ms) == FMRB_OK ? 0 : ERR_FAILED;
}

int32_t fmrb_bnd_uart_set_baudrate(int32_t unit, int32_t baudrate)
{
    uart_slot_t *s = own_slot(unit);
    if (!s) {
        return ERR_FAILED;
    }
    if (baudrate < FMRB_UART_BAUD_MIN || baudrate > FMRB_UART_BAUD_MAX) {
        return ERR_ARG;
    }
    uint32_t actual = (uint32_t)baudrate;
    if (fmrb_hal_uart_set_baud(s->hal, (uint32_t)baudrate, &actual) != FMRB_OK) {
        return ERR_FAILED;
    }
    return (int32_t)actual;
}

int32_t fmrb_bnd_uart_set_format(int32_t unit, int32_t data_bits, int32_t stop_bits, int32_t parity)
{
    uart_slot_t *s = own_slot(unit);
    if (!s) {
        return ERR_FAILED;
    }
    if (data_bits < 5 || data_bits > 8 || (stop_bits != 1 && stop_bits != 2) ||
        parity < FMRB_UART_PARITY_NONE || parity > FMRB_UART_PARITY_ODD) {
        return ERR_ARG;
    }
    return fmrb_hal_uart_set_format(s->hal, (uint8_t)data_bits, (uint8_t)stop_bits, (uint8_t)parity) == FMRB_OK
               ? 1 : ERR_FAILED;
}

int32_t fmrb_bnd_uart_dropped(int32_t unit)
{
    uart_slot_t *s = own_slot(unit);
    if (!s) {
        return ERR_FAILED;
    }
    return s->dropped > INT32_MAX ? INT32_MAX : (int32_t)s->dropped;
}

int32_t fmrb_bnd_uart_unit_mask(void)
{
    return (int32_t)fmrb_pin_manager_uart_app_units();
}

void fmrb_uart_release_owner(void *owner)
{
    if (owner == NULL || s_lock == NULL) {
        return;  // no app has opened a UART since the machine started
    }
    // A forced kill can leave the lock with the task that died holding it:
    // the ended app's own slots are touched by nobody else, so go on.
    bool locked = fmrb_semaphore_take(s_lock, FMRB_MS_TO_TICKS(200)) == pdTRUE;
    for (int unit = 0; unit < UART_SLOTS; unit++) {
        if (s_slots[unit].owner == owner) {
            close_slot(unit, &s_slots[unit], -1, -1);
        }
    }
    if (locked) {
        fmrb_semaphore_give(s_lock);
    }
}
