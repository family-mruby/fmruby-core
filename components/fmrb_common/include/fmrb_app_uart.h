#pragma once

/*
 * An app's UART (doc/app_uart): the answers of FmrbUart.open and the hook that
 * gives an ended app's UARTs back. The bodies are main/app/fmrb_boundary_uart.c;
 * the Ruby class UART (lib/gems/picoruby-fmrb-app/mrblib/fmrb-uart.rb) turns
 * an answer into its exception and text (sig/uart.rbs lists them).
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The answer of FmrbUart.open: 0 opened, else why not.
 *
 * A reason about one pin carries which in the next byte:
 * reason + 256 * FMRB_PIN_UART_WHICH_TX (1) or _RX (2).
 * 1-7 are the caller's mistakes (ArgumentError in Ruby), 8 and up what the
 * machine refuses (IOError).
 */
enum {
    FMRB_UART_OPENED      = 0,
    FMRB_UART_NO_UNIT     = 1,   // no such unit on this chip
    FMRB_UART_BAD_PIN     = 2,   // no such pin on this chip (which pin in the next byte)
    FMRB_UART_NO_PINS     = 3,   // neither TX nor RX given
    FMRB_UART_SAME_PIN    = 4,   // TX and RX on one pin
    FMRB_UART_BAD_BAUD    = 5,   // the rate is out of FMRB_UART_BAUD_MIN..MAX
    FMRB_UART_BAD_FORMAT  = 6,   // data bits 5-8, stop bits 1-2, parity 0-2
    FMRB_UART_BAD_BUFFER  = 7,   // the receive ring: a power of two, 16..16384
    FMRB_UART_SYSTEM_UNIT = 8,   // the system's unit (console, the link to the drawing side)
    FMRB_UART_UNIT_IN_USE = 9,   // another app has the unit open
    FMRB_UART_MIDI        = 10,  // the MIDI serial output holds the unit or the pin
    FMRB_UART_SYSTEM_PIN  = 11,  // a pin the system uses
    FMRB_UART_INPUT_ONLY  = 12,  // a line a chip drives, asked for TX
    FMRB_UART_PIN_IN_USE  = 13,  // held by another app, or for another use (I2C ...)
    FMRB_UART_FAILED      = 14,  // the driver did not open (or no memory)
    FMRB_UART_UNSUPPORTED = 15,  // no UART in this environment (the browser, the native program)
};

#define FMRB_UART_BAUD_MIN      300
#define FMRB_UART_BAUD_MAX      5000000
#define FMRB_UART_RING_MIN      16
#define FMRB_UART_RING_MAX      16384
#define FMRB_UART_WRITE_MAX     4096   // one FmrbUart.write; the Ruby class splits longer ones
#define FMRB_UART_BREAK_MAX_MS  10000

/**
 * @brief Close every UART the app (its task) still has open and give its pins
 *        back. Called when an app ends, on every target (sim included).
 */
void fmrb_uart_release_owner(void *owner);

#ifdef __cplusplus
}
#endif
