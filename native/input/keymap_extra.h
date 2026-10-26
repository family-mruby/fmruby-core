/*
 * keymap_extra.h - the extra key tables a machine definition can name
 * (input.keymap), for keyboards whose kernel driver sends key codes that do
 * not mean what linux_keys.c says.
 *
 * An extra table is consulted first for the devices it applies to
 * (input.keymap_device), and answers a HID usage and whether Shift has to be
 * held for it -- in US terms: such a device is decoded as US
 * (fmrb_host_send_key_*_us), the way the Tab5's built-in keyboard is, so a
 * symbol comes out the same whatever keyboard_layout says.
 *
 *   cardputer_zero  M5Stack Cardputer Zero (keymap_cardputer_zero.c)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True when the table has the code: *usage and *shift say what to send.
 * A code whose answer is usage 0 is swallowed (a layer key). */
typedef bool (*fmrb_keymap_extra_fn)(unsigned code, uint8_t *usage, bool *shift);

/* The table of that name, NULL when there is none. */
fmrb_keymap_extra_fn fmrb_keymap_extra_find(const char *name);

bool fmrb_keymap_cardputer_zero(unsigned code, uint8_t *usage, bool *shift);

#ifdef __cplusplus
}
#endif
