/*
 * input_wasm.h - what the host task tells the browser page through the
 * input backend (doc/kanji_input K4).
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The focused app's text cursor in frame coordinates (h the line height), or
 * that it has none. The page puts the browser's IME there. The host task. */
void fmrb_wasm_ime_publish_cursor(bool on, int fx, int fy, int h);

#ifdef __cplusplus
}
#endif
