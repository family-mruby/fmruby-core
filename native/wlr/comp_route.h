/*
 * comp_route.h - the compositor's decisions that need no wlroots
 * (doc/linux_native/, L8, L9a): where the Family mruby picture lies on the
 * output, where the Linux windows lie under it, what a point on the output
 * is over, and who has the keyboard.
 *
 * The compositor (output_wlr.c) owns one of these on its thread and asks it;
 * the host tests (test/native/) drive it with numbers alone.
 *
 *   picture   the frame (426x240, or 640x360 in the high-resolution mode)
 *             at the largest whole scale that fits the output, centred,
 *             black around (fmrb_present_place, the rule every output
 *             shares). It is drawn ABOVE the Linux windows.
 *   windows   Linux windows, each standing in for a Family mruby window
 *             (a stand-in app draws its title bar and frame, L9a). Family
 *             mruby places them: the content rectangle of the stand-in's
 *             window in frame coordinates (comp_place_t), in its stacking
 *             order. A window lies at that rectangle times the scale; it is
 *             asked to take that size. A window not placed yet, not shown,
 *             or any window while the frame is not the base size (the
 *             high-resolution mode) is hidden.
 *   holes     where the picture lets a window through: the pixels of the
 *             frame that hold COMP_HOLE_565 inside a shown window's
 *             rectangle. The stand-in fills its content with RGB332 0x01,
 *             which only the transparent key uses elsewhere; a Family mruby
 *             window or menu drawn over it covers the hole, so the stacking
 *             of the two kinds of windows is the picture's own.
 *   pointer   over a hole: the topmost shown window there (window-local
 *             coordinates at the output's own resolution). Anywhere else:
 *             Family mruby (the picture, or the black border, clamped into
 *             the frame as the SDL window does).
 *   keyboard  Family mruby's by default. Family mruby decides: the focus
 *             it places with (comp_route_place). A button pressed over a
 *             hole gives that window the keyboard at once (Family mruby
 *             gets the same press and agrees a moment later); pressed
 *             anywhere else gives it back to Family mruby.
 *
 * A window may be an xdg toplevel or an X11 window (Xwayland, L9b): the
 * same numbering, the same rules. A window may have a parent (a dialog: an
 * xdg toplevel's set_parent, X11's WM_TRANSIENT_FOR; L9c): it is a window
 * with a stand-in like any other, placed in the middle of its parent's and
 * kept above it by Family mruby's side (comp_dialog_place, wl_bridge.c).
 *
 * Menus and tooltips (xdg popups, X11 override-redirect windows) have no
 * stand-in: they lie over the picture, and are kept inside its rectangle
 * (comp_route_inside) -- never on the black around it.
 *
 * Window changes are queued as notices (comp_notice_t) for Family mruby's
 * side (native/wlr/wl_bridge.c) to take from another thread.
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
#define COMP_TEXT 64

/* RGB332 0x01 (FmrbApp::TRANSPARENT_COLOR) as the display composites it into
 * the RGB565 frame: the colour of a hole. */
#define COMP_HOLE_565 0x000Au

/* Who something goes to: Family mruby, or a window by its id (> 0). */
#define COMP_FMRB 0

typedef struct {
    int id;             /* > 0, the compositor's own numbering */
    int x, y;           /* top-left on the output (placed windows) */
    int w, h;           /* the size the window has (its geometry) */
    bool placed;        /* Family mruby has given it a place */
    bool shown;         /* Family mruby shows it (its window is not suspended) */
    bool visible;       /* placed, shown, and the frame is the base size */
    int fx, fy, fw, fh; /* the content rectangle, frame coordinates */
} comp_window_t;

/* One window's place as Family mruby gives it: the content rectangle of the
 * stand-in's window, frame coordinates. */
typedef struct {
    int id;
    int x, y, w, h;
    bool shown;
} comp_place_t;

typedef struct {
    int target;         /* COMP_FMRB or a window id */
    int x, y;           /* frame coordinates for Family mruby, window-local
                         * (output pixels) for a window */
    bool in_picture;    /* Family mruby only: inside the picture, not the border */
    int fx, fy;         /* a window: the frame pixel the point is in */
} comp_hit_t;

typedef enum {
    COMP_NOTICE_MAPPED = 1,     /* a window appeared (w, h; x: its parent's id, 0 none; text: its app id) */
    COMP_NOTICE_RESIZED,        /* its size changed (w, h) */
    COMP_NOTICE_UNMAPPED,       /* it went away */
    COMP_NOTICE_FOCUS,          /* the keyboard went to id (COMP_FMRB = Family mruby) */
    COMP_NOTICE_TITLE,          /* its title (text) */
    COMP_NOTICE_MOVE,           /* it asked to be moved: content top-left (x, y), frame coordinates */
    COMP_NOTICE_RESIZE,         /* it asked to be resized: content size (w, h) at (x, y), frame coordinates */
    COMP_NOTICE_FIT,            /* it settled on a size of its own (w, h, output pixels) other than the
                                 * one asked: its stand-in is to fit it (L9b) */
    COMP_NOTICE_PARENT,         /* its parent changed (x: the parent's id, 0 none; L9c) */
} comp_notice_kind_t;

typedef struct {
    comp_notice_kind_t kind;
    int id;
    int x, y;
    int w, h;
    char text[COMP_TEXT];
} comp_notice_t;

typedef struct {
    int out_w, out_h;                   /* the output, 0x0 before it is known */
    int fb_w, fb_h;                     /* the frame on hand, 0x0 before the first */
    int base_w, base_h;                 /* the frame's base size (0x0: any) */
    fmrb_present_place_t place;         /* the picture on the output */
    comp_window_t win[COMP_MAX_WINDOWS];/* bottom to top */
    int n;
    int focus;                          /* COMP_FMRB or a window id */

    /* The frame on hand (the caller's memory), for the holes. */
    const uint16_t *px;

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
 * the windows with it. True when anything moved. */
bool comp_route_layout(comp_route_t *r, int out_w, int out_h, int fb_w, int fb_h);

/* The frame's base size: windows show only while the frame is that size. */
void comp_route_set_base(comp_route_t *r, int base_w, int base_h);

/* The frame on hand, for the holes (px stays the caller's; it must hold
 * fb_w x fb_h pixels until the next call). */
void comp_route_set_frame(comp_route_t *r, const uint16_t *px);

/* A window appeared (on top, not placed yet; parent: its parent's id, 0
 * none) / changed size / went away. False when there is no room or no such
 * window. */
bool comp_route_window_add(comp_route_t *r, int id, int w, int h, int parent, const char *app_id);
bool comp_route_window_resize(comp_route_t *r, int id, int w, int h);
bool comp_route_window_remove(comp_route_t *r, int id);

/* Family mruby's places, bottom to top, and its focus (a window id, or
 * COMP_FMRB). Windows not in the list keep no place. Returns a mask of
 * COMP_PLACE_* for what changed. */
#define COMP_PLACE_MOVED 1      /* a window's rectangle or visibility */
#define COMP_PLACE_ORDER 2      /* the stacking */
#define COMP_PLACE_FOCUS 4      /* the keyboard */
int comp_route_place(comp_route_t *r, const comp_place_t *p, int n, int focus);

/* The window by id, NULL when there is none. */
const comp_window_t *comp_route_window(const comp_route_t *r, int id);

/* The size a placed window is asked to take (its rectangle times the scale);
 * false when it has no place. */
bool comp_route_wanted_size(const comp_route_t *r, int id, int *w, int *h);

/* Whether frame pixel (fx, fy) lets window id through. */
bool comp_route_hole(const comp_route_t *r, int id, int fx, int fy);

/* What the point (output coordinates) is over. */
comp_hit_t comp_route_at(const comp_route_t *r, int x, int y);

/* A button pressed at the point: the hit, with the keyboard moved by the
 * rule above (and a window raised among the windows). *focus_changed says
 * whether the keyboard moved. */
comp_hit_t comp_route_press(comp_route_t *r, int x, int y, bool *focus_changed);

/* Who has the keyboard. */
int comp_route_focus(const comp_route_t *r);

/* The picture's rectangle on the output, relative to (ox, oy): the box a
 * window's popups are kept in (xdg_positioner's constraint area, in the
 * coordinates of the window whose top-left is (ox, oy)). */
void comp_route_picture_box(const comp_route_t *r, int ox, int oy, int *x, int *y, int *w, int *h);

/* A w x h rectangle at (*x, *y) on the output moved the least that puts it
 * inside the picture's rectangle (its top-left kept in when it is larger).
 * True when it moved. */
bool comp_route_inside(const comp_route_t *r, int *x, int *y, int w, int h);

/* Where a dialog's stand-in window (ww x wh, frame coordinates) goes: its
 * content's middle on the parent's content middle (the parent's content
 * rectangle px, py, pw, ph), the content being the window less the frame
 * (left fl, top ft, the whole frame fw x fh); then moved the least that keeps
 * the window in the frame's base size, below the menu bar (top). */
void comp_dialog_place(int px, int py, int pw, int ph, int ww, int wh, int fl, int ft, int fw, int fh,
                       int base_w, int base_h, int top, int *x, int *y);

/* The frame (fb_w x fb_h RGB565) as the picture over the windows: ARGB8888,
 * opaque, except the holes, which are fully transparent. */
void comp_route_compose(const comp_route_t *r, const uint16_t *px, uint32_t *out);

/* Queue a notice (the compositor's thread). text may be NULL. */
void comp_route_notice(comp_route_t *r, comp_notice_kind_t kind, int id, int x, int y, int w, int h,
                       const char *text);

/* Take the oldest notice. False when there is none. Any thread. */
bool comp_route_take_notice(comp_route_t *r, comp_notice_t *out);

#ifdef __cplusplus
}
#endif
