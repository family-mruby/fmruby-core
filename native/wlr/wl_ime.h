/*
 * wl_ime.h - the PC's IME in the compositor (doc/linux_native, L12b). See
 * wl_ime.c. Main thread only.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "comp_route.h"

struct wl_display;
struct wlr_seat;
struct wlr_scene_tree;
struct wlr_surface;
struct wlr_keyboard;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* The output position of a Linux window's surface origin, false when the
     * surface is no window's (or it is not shown). */
    bool (*surface_origin)(struct wlr_surface *surface, int *x, int *y);
    /* A new keyboard: the IME's virtual one (keys it hands back). */
    void (*add_keyboard)(struct wlr_keyboard *kb);
} wl_ime_host_t;

/* Serves input-method-v2, text-input-v3 and virtual-keyboard-v1 on display;
 * the IME's popups and the text being composed over Family mruby go into
 * layer (over the picture and every popup). route: the picture's place. */
void wl_ime_init(struct wl_display *display, struct wlr_seat *seat, struct wlr_scene_tree *layer,
                 const comp_route_t *route, const wl_ime_host_t *host);

/* Start the IME program (compositor.ime: "" none; the first word must be
 * found in PATH, else one line says Family mruby keeps its kana input). */
void wl_ime_start(const char *command, const char *socket);

/* The keyboard focus moved: a Linux window's surface, or Family mruby
 * (surface NULL, fmrb true), or nobody. */
void wl_ime_focus(struct wlr_surface *surface, bool fmrb);

/* Keys: true while the IME takes this keyboard's keys (it grabbed the
 * keyboard and serves the focused text input; never for its own virtual
 * keyboard, which hands back the keys it does not use). */
bool wl_ime_takes_keys(struct wlr_keyboard *kb);
/* One key to the IME (false when it no longer takes keys). */
bool wl_ime_key(struct wlr_keyboard *kb, uint32_t time_msec, uint32_t code, uint32_t state);
/* The modifiers to the IME instead of the window, true when it took them. */
bool wl_ime_modifiers(struct wlr_keyboard *kb);
/* A keyboard of the IME's own (its virtual keyboard). */
bool wl_ime_own_keyboard(struct wlr_keyboard *kb);

/* Each loop: Family mruby's text cursor, the popups' places. */
void wl_ime_pump(void);
/* The picture moved or was scaled: everything placed again. */
void wl_ime_relayout(void);
void wl_ime_fini(void);

/* The control socket's "comp ime". */
void wl_ime_describe(char *out, size_t cap);

#ifdef __cplusplus
}
#endif
