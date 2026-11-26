#pragma once

/**
 * @file fmrb_midi_serial.h
 * @brief Serial (UART) MIDI output owned by the system side.
 *
 * The port is opened once and shared by every app VM that asks for it, so
 * no app can take the UART away from another (see doc/midi/README.md
 * direction E-1). Ruby only ever sees a MIDI::Device transport. When the
 * last app that asked for it ends, the system closes the port and frees its
 * UART and pin (fmrb_midi_serial_release_owner), so an app's UART can use
 * them (doc/app_uart plan 7.3).
 *
 * Everything above the wire - which MIDI bytes a USB-MIDI packet turns into
 * - lives in Ruby (lib/gems/picoruby-fmrb-midi/mrblib/fmrb-serial-midi.rb) so
 * it can be tested on the host without a UART. This layer only opens the
 * port and writes bytes.
 *
 * The same code serves both targets through fmrb_hal_uart: a device path on
 * POSIX (a FIFO under the bind-mounted project directory in the simulation)
 * and uart_num + pins on ESP32.
 */

#include "fmrb_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Standard MIDI DIN baud, which the SAM2695 also speaks. */
#define FMRB_MIDI_SERIAL_BAUD 31250

typedef struct {
    const char *device_path; /* POSIX only: FIFO or tty to write to */
    int uart_num;            /* ESP32 only */
    int tx_pin;              /* ESP32 only */
    int rx_pin;              /* ESP32 only, -1 to disable input */
    uint32_t baud_rate;      /* 0 means FMRB_MIDI_SERIAL_BAUD */
} fmrb_midi_serial_config_t;

/**
 * @brief Open the shared MIDI port, or accept the one already open.
 *
 * Calling this again while the port is open succeeds without reopening, so
 * several apps can hold a transport at once. The calling task is recorded
 * as one of the port's owners.
 *
 * @param config Configuration, or NULL for the platform defaults
 * @return FMRB_OK on success
 */
fmrb_err_t fmrb_midi_serial_open(const fmrb_midi_serial_config_t *config);

/** @brief Close the shared port (for every app that holds it). */
fmrb_err_t fmrb_midi_serial_close(void);

/**
 * @brief An app ended: drop it from the port's owners, closing the port when
 *        it was the last.
 *
 * Called from the app clean-up paths, next to fmrb_uart_release_owner, after
 * the app's VM is gone. What the app still has in the MIDI queue is dropped
 * first, so its notes are ended while the port is open.
 *
 * @param owner  The app's task handle
 * @param app_id The app's id (the queue's src_pid)
 */
void fmrb_midi_serial_release_owner(void *owner, uint8_t app_id);

/** @brief Whether the port is open and usable. */
bool fmrb_midi_serial_is_open(void);

/**
 * @brief Write raw MIDI bytes.
 * @return FMRB_OK on success
 */
fmrb_err_t fmrb_midi_serial_write(const uint8_t *data, size_t len);

/** @brief Fill config with the defaults for this board / build. */
void fmrb_midi_serial_default_config(fmrb_midi_serial_config_t *config);

#ifdef __cplusplus
}
#endif
