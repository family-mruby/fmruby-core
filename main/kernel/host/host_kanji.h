#pragma once

// Kanji conversion in the input path (doc/kanji_input/, K3). Host task only.
//
// Beside the kana composer (fmrb_kana): while a dictionary is open
// (kanji_lookup) and kana input is on, the kana the composer commits are not
// sent to the app at once but kept as the reading being typed (the preedit).
// Space or the JIS Henkan key looks the reading up and shows the candidates
// in a line at the bottom of the screen, a canvas of the system's own drawn
// from here. Enter, a number key or typing on commits; Esc goes back to the
// reading, and again drops it. What is committed reaches the app as the same
// one-byte key events a composed kana does, so no app changes.
//
// Without a dictionary none of this engages and kana input is as before.

#include <stdbool.h>
#include <stdint.h>

#include "fmrb_kana.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HOST_KANJI_PASS = 0,     // forward the event as it is
    HOST_KANJI_CONSUME,      // swallow it
    HOST_KANJI_CLEAR_CHAR,   // forward it with character cleared
} host_kanji_action_t;

/** @brief True when key events go through host_kanji_key. */
bool host_kanji_engaged(void);

/** @brief True while a lookup is outstanding: key events wait for it. */
bool host_kanji_waiting(void);

/**
 * @brief One key event, after the kana composer has seen it.
 *
 * @param pid     The focused app (where committed text goes).
 * @param action  What the composer said about the event.
 * @param kana    The kana the composer committed with it.
 */
host_kanji_action_t host_kanji_key(uint8_t pid, bool key_down, uint8_t scancode,
                                   uint8_t modifier, char ch,
                                   fmrb_kana_action_t action,
                                   const fmrb_kana_result_t *kana);

/** @brief Kana mode changed: an unfinished reading is committed as it shows. */
void host_kanji_mode_changed(uint8_t pid);

/** @brief The lookup task answered request seq (HOST_MSG_KANJI_RESULT). */
void host_kanji_result(uint32_t seq);

/** @brief From the host loop: deferred drawing, the focus, a lost answer. */
void host_kanji_tick(void);

#ifdef __cplusplus
}
#endif
