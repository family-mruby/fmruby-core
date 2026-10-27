/*
 * comp_link.h - the hand-overs between the compositor and Family mruby's
 * window manager (doc/linux_native/, L9a). Plain C, no wlroots, no FreeRTOS:
 * the compositor runs on the program's main thread, the other side
 * (wl_bridge.c) is a FreeRTOS task, and each only takes a lock long enough
 * to copy.
 *
 *   notices   compositor -> Family mruby: windows that appeared, changed,
 *             went away, retitled, or asked to be moved or resized
 *             (comp_route's queue, taken through here)
 *   places    Family mruby -> compositor: where each window's stand-in is
 *             (its content rectangle in frame coordinates), the stacking and
 *             the focus. The newest set wins; the compositor is woken.
 *             (The part of the frame to show -- a fullscreen app smaller
 *             than the screen -- goes with each frame: native_frame.h.)
 *   commands  Family mruby -> compositor: ask a window to close. Queued.
 *   geometry  compositor -> Family mruby: the picture's scale and the
 *             frame's base size, to size a new stand-in.
 *
 * Nothing is attached (comp_link_attached() is false) unless the program
 * runs as the compositor.
 */
#pragma once

#include <stdbool.h>

#include "comp_route.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int n;
    comp_place_t p[COMP_MAX_WINDOWS];   /* bottom to top */
    int focus;                          /* a window id, or COMP_FMRB */
} comp_places_t;

typedef enum {
    COMP_CMD_CLOSE = 1,                 /* ask the window to close (xdg close) */
} comp_cmd_kind_t;

/* The compositor's side: attach its route (the notices) and the descriptor
 * a write wakes its loop with; detach at the end. */
void comp_link_attach(comp_route_t *r, int wake_fd);
void comp_link_detach(void);
bool comp_link_attached(void);

void comp_link_set_geometry(int scale, int base_w, int base_h);
bool comp_link_geometry(int *scale, int *base_w, int *base_h);

bool comp_link_take_notice(comp_notice_t *out);

/* Family mruby's side: publish places (only when they differ from the last,
 * so the compositor wakes for changes). */
void comp_link_place(const comp_places_t *pl);
/* The compositor's side: true and a copy when the places changed since the
 * last take. */
bool comp_link_take_places(comp_places_t *out);

void comp_link_command(comp_cmd_kind_t kind, int id);
bool comp_link_take_command(comp_cmd_kind_t *kind, int *id);

#ifdef __cplusplus
}
#endif
