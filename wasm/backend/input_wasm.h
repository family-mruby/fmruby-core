/*
 * input_wasm.h - what the host task tells the browser page through the
 * input backend (doc/kanji_input K4).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The focused app's text cursor in frame coordinates (h the line height), or
 * that it has none. The page puts the browser's IME there. The host task. */
void fmrb_wasm_ime_publish_cursor(bool on, int fx, int fy, int h);

/* The page's last guess at whether the browser's IME is on (doc/kanji_input
 * K4b), and in *seq a count that moves on with every guess. The host task. */
bool fmrb_wasm_ime_guess(uint32_t *seq);

#ifdef __cplusplus
}
#endif
