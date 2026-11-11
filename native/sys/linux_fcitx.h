/*
 * linux_fcitx.h - fcitx5's state over D-Bus, for the kana indicator
 * (doc/kanji_input, the native program's indicator): whether the PC's IME
 * types Japanese now or letters.
 *
 * fcitx5 (5.1) tells no signal when it switches; it is asked
 * (org.fcitx.Fcitx5 /controller org.fcitx.Fcitx.Controller1: State and
 * CurrentInputMethod, both about the input context that had the focus
 * last) whenever the state can have changed: linux_fcitx_ask, which the
 * IME's side calls on the keys it sees, on focus moves and on activation.
 * One question goes a little after the call and one a little later (the
 * key travels to fcitx5 on another socket than the question), and while
 * linux_fcitx_watch is on, one every LINUX_FCITX_POLL_MS as well (switches
 * nobody here saw: the SDL window does not see the keys the IME takes).
 *
 * A thread of its own with a session-bus connection of its own (sd-bus,
 * linux_bus.h), outside FreeRTOS. The calls below only set flags and write
 * to its wake descriptor: no caller ever waits on the bus. The questions go
 * with NO_AUTO_START, so a fcitx5 that is not running is not started by the
 * bus (its package installs a D-Bus service file).
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINUX_FCITX_POLL_MS 1000

typedef enum {
    LINUX_FCITX_ABSENT = 0,     /* no fcitx5 on the session bus (or no bus) */
    LINUX_FCITX_NO_CONTEXT,     /* fcitx5 is there, no input context had the focus yet */
    LINUX_FCITX_LETTERS,        /* a keyboard layout (keyboard-us ...): letters */
    LINUX_FCITX_JAPANESE,       /* an input method engine (mozc ...): Japanese input */
} linux_fcitx_answer_t;

/* On the thread, after each question: the answer and the input method's
 * name ("" when none). */
typedef void (*linux_fcitx_cb_t)(linux_fcitx_answer_t answer, const char *im);

/* State (0 no context, 1 inactive, 2 active) and CurrentInputMethod made
 * one answer: the input method's name says it when there is one
 * (keyboard-* are layouts, anything else an engine), else the state. */
linux_fcitx_answer_t linux_fcitx_judge(int state, const char *im);

/* Start the thread (once; later calls only return what the first did).
 * False and one line when there is no libsystemd; a missing session bus is
 * one ABSENT answer from the thread. */
bool linux_fcitx_start(linux_fcitx_cb_t cb);

/* Any thread: the state may have changed, ask soon. */
void linux_fcitx_ask(void);

/* Any thread: ask every LINUX_FCITX_POLL_MS too (while an IME is there). */
void linux_fcitx_watch(bool on);

#ifdef __cplusplus
}
#endif
