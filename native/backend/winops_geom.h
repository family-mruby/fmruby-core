/*
 * winops_geom.h - where the PC's window operations put a window
 * (doc/linux_native/, L13), plain C: no RTOS, no allocation, so the host
 * tests (test/native/comp.c) drive it with numbers alone. The task side is
 * native_winops.c.
 *
 *   work area   the screen below the menu bar (the taskbar is in the menu
 *               bar), the whole width
 *   maximize    the work area
 *   left/right  its left or right half (the right one takes the odd pixel)
 *   title bar   where a double-click maximizes: the title bar's rows, right
 *               of the window menu's button and left of the close button
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int x, y, w, h;
} winops_rect_t;

typedef enum {
    WINOPS_NONE = 0,    /* as the user left it */
    WINOPS_MAX,         /* the work area */
    WINOPS_LEFT,        /* its left half */
    WINOPS_RIGHT,       /* its right half */
} winops_layout_t;

/* The menu bar's height and the title bar's (FmrbApp's TITLE_BAR_H). */
#define WINOPS_MENU_BAR_H 13
#define WINOPS_TITLE_H 11
/* The title bar's buttons: the window menu at the left, the close button at
 * the right; a double-click on either is not one on the title. */
#define WINOPS_TITLE_LEFT 14
#define WINOPS_TITLE_RIGHT 12
/* Two presses make a double-click within this long and this near. */
#define WINOPS_DOUBLE_MS 400
#define WINOPS_DOUBLE_NEAR 4

/* The rectangle a layout gives on a screen_w x screen_h screen (layout not
 * NONE). */
winops_rect_t winops_layout_rect(winops_layout_t layout, int screen_w, int screen_h);

/* Whether (rx, ry), relative to a window of width w, is on its title. */
bool winops_on_title(int rx, int ry, int w);

/* Whether a second press (t2 ms, at x2, y2) makes a double-click with the
 * first (t1, x1, y1). */
bool winops_double(unsigned t1, int x1, int y1, unsigned t2, int x2, int y2);

#ifdef __cplusplus
}
#endif
