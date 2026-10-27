/*
 * comp_route.h - the compositor's decisions that need no wlroots
 * (doc/linux_native/, L8): where the Family mruby picture lies on the
 * output, where the Linux windows lie on top of it, what a point on the
 * output is over, and who has the keyboard.
 *
 * The compositor (output_wlr.c) owns one of these on its thread and asks it;
 * the host tests (test/native/) drive it with numbers alone.
 *
 *   picture   the frame (426x240, or 640x360 in the high-resolution mode)
 *             at the largest whole scale that fits the output, centred,
 *             black around (fmrb_present_place, the rule every output
 *             shares).
 *   windows   Linux windows at their own size (no scaling), centred on the
 *             output for this prototype, in stacking order (the last one on
 *             top). Kernel-decided places come in L9.
 *   pointer   a point is over the topmost window that contains it, else
 *             over Family mruby (the picture, or the black border, clamped
 *             into the frame as the SDL window does).
 *   keyboard  Family mruby's by default. A button pressed over a window
 *             gives that window the keyboard and raises it; pressed over
 *             the picture or the border gives it back to Family mruby. A
 *             window that goes away gives it back too.
 *
 * Window changes are also queued as notices (comp_notice_t) for the kernel's
 * window manager to take from another thread in L9; nothing takes them in
 * L8 but the tests.
 */
#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "fmrb_present.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMP_MAX_WINDOWS 16
#define COMP_NOTICES 64

/* Who something goes to: Family mruby, or a window by its id (> 0). */
#define COMP_FMRB 0

typedef struct {
    int id;             /* > 0, the compositor's own numbering */
    int x, y;           /* top-left on the output */
    int w, h;           /* the window's own size */
} comp_window_t;

typedef struct {
    int target;         /* COMP_FMRB or a window id */
    int x, y;           /* frame coordinates for Family mruby, window-local
                         * for a window */
    bool in_picture;    /* Family mruby only: inside the picture, not the border */
} comp_hit_t;

typedef enum {
    COMP_NOTICE_MAPPED = 1,     /* a window appeared (w, h) */
    COMP_NOTICE_RESIZED,        /* its size changed (w, h) */
    COMP_NOTICE_UNMAPPED,       /* it went away */
    COMP_NOTICE_FOCUS,          /* the keyboard went to id (COMP_FMRB = Family mruby) */
} comp_notice_kind_t;

typedef struct {
    comp_notice_kind_t kind;
    int id;
    int w, h;
} comp_notice_t;

typedef struct {
    int out_w, out_h;                   /* the output, 0x0 before it is known */
    int fb_w, fb_h;                     /* the frame on hand, 0x0 before the first */
    fmrb_present_place_t place;         /* the picture on the output */
    comp_window_t win[COMP_MAX_WINDOWS];/* bottom to top */
    int n;
    int focus;                          /* COMP_FMRB or a window id */

    /* The notices, for another thread: the compositor's thread writes, the
     * reader takes under the lock. When the reader falls COMP_NOTICES
     * behind, the oldest are dropped (and counted). */
    pthread_mutex_t lock;
    comp_notice_t notice[COMP_NOTICES];
    unsigned notice_wr, notice_rd;
    unsigned notices_dropped;
} comp_route_t;

void comp_route_init(comp_route_t *r);
void comp_route_destroy(comp_route_t *r);

/* The output's size or the frame's changed: the picture is placed again and
 * the windows centred again. True when anything moved. */
bool comp_route_layout(comp_route_t *r, int out_w, int out_h, int fb_w, int fb_h);

/* A window appeared (on top, centred) / changed size / went away. False
 * when there is no room or no such window. */
bool comp_route_window_add(comp_route_t *r, int id, int w, int h);
bool comp_route_window_resize(comp_route_t *r, int id, int w, int h);
bool comp_route_window_remove(comp_route_t *r, int id);

/* The window by id, NULL when there is none. */
const comp_window_t *comp_route_window(const comp_route_t *r, int id);

/* What the point (output coordinates) is over. */
comp_hit_t comp_route_at(const comp_route_t *r, int x, int y);

/* A button pressed at the point: the hit, with the keyboard moved by the
 * rule above. *focus_changed says whether it moved. */
comp_hit_t comp_route_press(comp_route_t *r, int x, int y, bool *focus_changed);

/* Who has the keyboard. */
int comp_route_focus(const comp_route_t *r);

/* Take the oldest notice. False when there is none. Any thread. */
bool comp_route_take_notice(comp_route_t *r, comp_notice_t *out);

#ifdef __cplusplus
}
#endif
