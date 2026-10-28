/*
 * native_winops.h - the PC's window operations (doc/linux_native/, L13):
 * maximize, the left and right halves, and back, for Family mruby's windows
 * and (through their stand-ins, native/wlr/wl_bridge.c) the Linux windows.
 *
 *   double-click  on a resizable window's title (winops_geom.h): the work
 *                 area, and back. The second press and its release are not
 *                 passed on (the window manager would start a drag there).
 *   Super+arrows  on the window in front (the one with the keyboard): left
 *                 and right halves, up maximizes, down puts it back.
 *   a Linux window's own request (its maximize button): wl_bridge.c.
 *
 * Each operation moves and resizes the window through the calls the window
 * manager's own drags make (fmrb_app_update_window_position / _size). The
 * size and place before the first operation are kept per window, so "back"
 * returns there; a window the user then moves or resizes is as the user
 * left it, and forgets them.
 *
 * Only where the machine definition says desktop.window_ops (the PC's
 * machines); elsewhere every call answers "not taken". The devices, the
 * simulation and the browser have none of this (native program only).
 * Called from FreeRTOS tasks (the input sources, the compositor's bridge).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "winops_geom.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WINOPS_DO_MAX = 1,
    WINOPS_DO_LEFT,
    WINOPS_DO_RIGHT,
    WINOPS_DO_RESTORE,
    WINOPS_DO_TOGGLE,       /* maximized: back; else maximize */
} winops_do_t;

/* main_native.c, before the machine starts. */
void native_winops_setup(bool enabled);
bool native_winops_enabled(void);

/* Do it on the window of app pid. why names the cause for the log. True when
 * the window changed. */
bool native_winops_apply(int pid, winops_do_t op, const char *why);

/* On the window in front (the keyboard's), not the desktop's. */
bool native_winops_front(winops_do_t op, const char *why);

/* How the window of app pid is laid out (WINOPS_NONE when not by these). */
winops_layout_t native_winops_layout(int pid);

/* A mouse button on its way to Family mruby (frame coordinates): true when
 * it was a double-click's on a title, taken here (not to be passed on). */
bool native_winops_button(int x, int y, int button, bool down);

/* A key on its way to Family mruby (HID usage): true when taken here (an
 * arrow with Super held, and its release). */
bool native_winops_key(uint8_t usage, bool down);

/* The arrow's operation (HID usage of an arrow key), 0 for another key. */
winops_do_t native_winops_arrow(uint8_t usage);

#ifdef __cplusplus
}
#endif
