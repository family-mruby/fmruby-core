/*
 * native_ime.h - the PC's IME for the Family mruby apps (doc/linux_native,
 * L12b): the hand-overs between the program's main thread, which talks to
 * the IME (the compositor's input-method, native/wlr/wl_ime.c; SDL's text
 * input, native/output/output_sdl.c), and the machine's host task.
 *
 *   present  main thread -> host task: an IME types for the apps. While it
 *            does, Family mruby's own kana input steps aside (host_task.c).
 *   cursor   host task -> main thread: the focused app's text cursor in
 *            frame coordinates (FmrbApp.set_text_cursor), where the text
 *            being composed and the candidates are shown.
 *   commit   main thread -> host task: text the IME committed, through the
 *            input ring (native_input.h), in order with the keys.
 *   japanese what the IME types now, Japanese or letters (host task: the
 *            kana indicator shows the IME's mark or A). fcitx5 is asked
 *            over D-Bus (linux_fcitx.h) when it is on the session bus;
 *            else the keys are guessed from, as the browser build does:
 *            text being composed or non-ASCII text committed says
 *            Japanese, a plain key the IME let through says letters.
 *
 * Plus the drawing of the text being composed in Family mruby's own font
 * (native_preedit.cpp), for the outputs that put it over the picture.
 *
 * The main thread never calls into FreeRTOS: everything here is atomics, a
 * pthread lock held to copy, and the input ring.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Main thread: an IME is there (or no longer). Logged on a change. */
void native_ime_set_present(bool on, const char *what);

/* Any thread: what the IME types may have changed (a key went to it, the
 * focus moved, it was activated): fcitx5 is asked again soon. Never waits. */
void native_ime_recheck(void);

/* Any thread: a sign from the keys (sign: for the log line). Ignored
 * while fcitx5 answers on the bus. */
void native_ime_guess(bool japanese, const char *sign);

/* Main thread: the focused app's text cursor (frame coordinates; h is the
 * line's height). False when it takes no text now. seq moves on every
 * change. */
bool native_ime_cursor(int *fx, int *fy, int *h, unsigned *seq);

/* Main thread: text the IME committed (UTF-8, NUL-terminated), to the
 * focused app. */
void native_ime_commit(const char *utf8);

/* The input task: one NATIVE_INPUT_TEXT event's bytes, to the host. */
void native_ime_deliver(uint32_t a, uint32_t b, uint32_t c, uint32_t d);

/* Counters (the control socket's "ime"). */
typedef struct {
    bool present;
    bool japanese;              /* the IME types Japanese now */
    bool from_fcitx;            /* fcitx5's answers decide (else the keys' signs) */
    unsigned commits;           /* texts committed to Family mruby */
    unsigned commit_bytes;
    unsigned cursor_changes;
} native_ime_stats_t;
void native_ime_stats(native_ime_stats_t *out);

/* The text being composed, drawn in Family mruby's font: efontJA 16 for a
 * line of 16 or more, 12 below (8 for a line under 12: misaki), black on
 * white with a line under it; the part [sel_begin, sel_end) (byte offsets
 * into utf8) inverted, and a caret at sel_begin when the two are equal and
 * not at the end. ARGB8888 into out (room for cap pixels); its size in
 * *w x *h. False when it does not fit or there is nothing to draw. Any one
 * thread at a time. */
bool native_preedit_render(const char *utf8, int sel_begin, int sel_end, int line_h,
                           uint32_t *out, int cap, int *w, int *h);

#ifdef __cplusplus
}
#endif
