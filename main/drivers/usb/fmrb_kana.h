/**
 * @file fmrb_kana.h
 * @brief Romaji to kana composition for the JP keyboard layout.
 *
 * Sits at the single point where a scancode becomes a character
 * (host_task.c, right after fmrb_keymap_scancode_to_char), so every input
 * source - USB, Tab5 built-in, I2C, remote desktop, SDL - goes through it.
 *
 * There is no dictionary and no kanji conversion: two keystrokes become one
 * kana, which is what a child needs to type Japanese into the editor. The
 * committed kana leaves as UTF-8 bytes, one key event per byte, because the
 * HID key payload carries a single byte of `character` and widening it would
 * break every app that reads it.
 *
 * Scancode and keycode are never touched. A game that reads scancodes keeps
 * working with kana mode on.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FMRB_KANA_OFF = 0,       // romaji passes through as ASCII
    FMRB_KANA_HIRAGANA = 1,
    FMRB_KANA_KATAKANA = 2,
} fmrb_kana_mode_t;

// Cycled by the on-screen indicators: off -> hiragana -> katakana -> off.
#define FMRB_KANA_MODE_COUNT 3

typedef enum {
    FMRB_KANA_PASS = 0,     // forward the event untouched
    FMRB_KANA_CONSUME,      // swallow it (mode toggle, or a backspace that
                            // only erased the pending consonant)
    FMRB_KANA_COMPOSE,      // forward it with character cleared, then send
                            // result->out as its own key events
} fmrb_kana_action_t;

typedef struct {
    uint8_t out[12];        // UTF-8 of the committed kana (up to 2 kana)
    uint8_t out_len;
    bool mode_changed;      // caller notifies the app
} fmrb_kana_result_t;

/**
 * @brief Feed one key event to the composer.
 *
 * @param key_down true for a press, false for a release. Only presses compose;
 *                 releases of composing keys still return FMRB_KANA_COMPOSE
 *                 with out_len 0 so the ASCII character stays suppressed.
 * @param scancode USB HID scancode of the event.
 * @param modifier FMRB_KEYMAP_MOD_* bitmap.
 * @param ascii    The character the keymap produced (0 when there is none).
 * @param result   Receives the committed bytes; zeroed first.
 * @return What the caller should do with the event.
 */
fmrb_kana_action_t fmrb_kana_feed(bool key_down, uint8_t scancode,
                                  uint8_t modifier, char ascii,
                                  fmrb_kana_result_t *result);

/** @brief Current mode (FMRB_KANA_OFF when kana input is not active). */
fmrb_kana_mode_t fmrb_kana_get_mode(void);

/**
 * @brief Set the mode outright.
 *
 * For the on-screen indicators, which are clickable: a keyboard is not the
 * only way to reach kana input, and on a touch machine it is not even the
 * usual one. Pending romaji is dropped. Callers that want the apps to hear
 * about it should go through fmrb_host_set_kana_mode() instead.
 */
void fmrb_kana_set_mode(fmrb_kana_mode_t mode);

/** @brief Drop the pending consonant. Mode is kept. */
void fmrb_kana_clear_pending(void);

#ifdef __cplusplus
}
#endif
