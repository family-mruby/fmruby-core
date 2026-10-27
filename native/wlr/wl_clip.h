/*
 * wl_clip.h - the machine's clipboard as the compositor's Wayland selection
 * (doc/linux_native, L12a). See wl_clip.c. Main thread only.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

struct wl_display;
struct wl_event_loop;
struct wlr_seat;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int owner;          /* the selection now: 0 none, 1 Family mruby's, 2 a Linux program's */
    size_t kept_len;    /* the last text either way */
    unsigned gave;      /* texts from Family mruby made the selection */
    size_t last_gave;
    unsigned served;    /* pastes into Linux programs */
    unsigned linux_sets;/* selections set by Linux programs */
    unsigned took;      /* ...read and handed to Family mruby */
    size_t last_took;
    unsigned refused;   /* ...over the maximum */
    unsigned not_text;  /* ...offering no text */
    unsigned timeouts;
    unsigned owned;     /* times the compositor became the owner */
    unsigned kept;      /* ...of which to keep a text whose owner went away */
} wl_clip_stats_t;

void wl_clip_init(struct wl_display *display, struct wl_event_loop *loop, struct wlr_seat *seat);
/* Family mruby's new text into the selection, and the selection kept. */
void wl_clip_pump(void);
void wl_clip_fini(void);
void wl_clip_stats(wl_clip_stats_t *out);

#ifdef __cplusplus
}
#endif
