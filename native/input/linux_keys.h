/*
 * linux_keys.h - Linux key codes (KEY_* of <linux/input-event-codes.h>) to
 * the USB HID usage IDs the firmware's key tables are written in
 * (fmrb_keymap.c), and the modifier bits.
 *
 * One table for every Linux input path: evdev (input_evdev.c) and, later,
 * wlroots (L8/L9), which hands out the same codes. It is the inverse of the
 * kernel's own HID keyboard table (drivers/hid/hid-input.c), so a USB
 * keyboard's key comes back to the usage it was sent as. Keys outside the
 * HID keyboard page (media keys, brightness) have no usage here and answer 0.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The HID usage (keyboard page) of a Linux key code, 0 when it has none. */
uint8_t fmrb_linux_key_to_hid(unsigned code);

/* The FMRB_KEYMAP_MOD_* bit a Linux modifier key holds (Shift, Ctrl, Alt;
 * left and right apart), 0 for every other key. The Meta keys have usages
 * (0xE3 / 0xE7) but no bit. */
uint8_t fmrb_linux_key_mod_bit(unsigned code);

#ifdef __cplusplus
}
#endif
