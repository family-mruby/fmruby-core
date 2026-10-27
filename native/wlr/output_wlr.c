/*
 * output_wlr.c - the program as a Wayland compositor (doc/linux_native/,
 * L8, L9a): the "wlroots" output and input of native_backends.h. Started
 * from tinywl (wlroots 0.19's smallest example).
 *
 * Threads. wlroots runs on the program's main thread, which is not a
 * FreeRTOS task (main_native.c): pump() is wl_event_loop_dispatch. The
 * machine's threads never call wlroots. The hand-overs:
 *
 *   picture  the display task puts each composited frame into native_frame
 *            (its lock, a copy) and writes an eventfd this file has added to
 *            the event loop, so the dispatch returns as soon as a frame is
 *            there; the main loop copies it out and calls present(), which
 *            turns it into a wlr_buffer for the scene.
 *   input    the seat's events meant for Family mruby go into the main-
 *            thread ring of native_input.h (the SDL window's: one writer,
 *            atomics, no lock); the machine's input task drains it.
 *   windows  comp_link.h, with Family mruby's side in wl_bridge.c: notices
 *            of the Linux windows out (appeared, retitled, asked to move or
 *            resize, gone), places, stacking, focus and close requests in.
 *            The same eventfd wakes this loop for them.
 *
 * Scene, bottom to top: black over the whole output; the Linux windows,
 * each where Family mruby's stand-in for it has its content (the content
 * rectangle times the picture's scale), clipped to it, in Family mruby's
 * stacking; the picture at the largest whole scale (nearest-neighbour, by
 * the renderer), transparent where a stand-in's content is on screen (the
 * holes, comp_route.h); the popups of the Linux windows, over everything.
 * A Family mruby window or menu over a stand-in covers the Linux window
 * there, so the two kinds of windows stack freely.
 *
 * Pointer: over a hole (or a popup) it goes to that Linux window at the
 * output's own resolution, with wlroots' cursor (Family mruby's is hidden);
 * a press there also goes to Family mruby, whose window manager raises and
 * focuses the stand-in as for any click. Anywhere else it goes to Family
 * mruby. Keys go where Family mruby's focus is (a stand-in's: its Linux
 * window), except Ctrl+Tab / Ctrl+comma, which are always Family mruby's
 * (switching windows). The windows' keymap follows keyboard_layout.
 *
 * Linux windows are told to draw no decoration of their own
 * (xdg-decoration, server side): the stand-in draws the frame. Maximize and
 * fullscreen requests are answered without a change; move and resize
 * requests become moves and resizes of the stand-in.
 *
 * Also served: wlr-screencopy and xdg-output, so grim takes the composited
 * screen; and requests on the control socket ("comp ...", see wlr_ctl
 * below) that drive a keyboard and a pointer of the tools' own through the
 * same handlers as real devices, and report sizes and timings.
 *
 * compositor.autostart, when set, is started once through /bin/sh -c in a
 * session of its own with WAYLAND_DISPLAY naming this compositor (also set
 * in the program's environment, so the Shell's "!" programs inherit it).
 *
 * The end (close): every Linux window is asked to close, and the loop goes
 * on -- Family mruby still on screen -- until they have all gone or
 * LINGER_MS has passed, so a program can ask about unsaved work. Then the
 * clients still connected get SIGTERM, and SIGKILL half a second later;
 * the autostart's session last.
 */

#define _GNU_SOURCE
#define WLR_USE_UNSTABLE 1

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/gles2.h>
#include <wlr/render/pixman.h>
#include <wlr/render/vulkan.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/version.h>
#include <xkbcommon/xkbcommon.h>

#include "comp_link.h"
#include "comp_route.h"
#include "fmrb_keymap.h"
#include "linux_keys.h"
#include "native_backends.h"
#include "native_frame.h"
#include "native_input.h"

/* Pointer motion to Family mruby is passed on at most this often (the
 * machine redraws its cursor per event), as from the SDL window. */
#define MOTION_INTERVAL_MS 16
/* How long the end waits for the Linux windows to close when asked. */
#define LINGER_MS 5000
/* After the last window, how long its program has to disconnect by itself. */
#define CLIENT_LEAVE_MS 1000
/* Then SIGTERM, and SIGKILL this much later. */
#define TERM_GRACE_MS 500

/* ------------------------------------------------------------- state */

typedef struct window {
    struct wl_list link;
    int id;
    struct wlr_xdg_toplevel *toplevel;
    struct wlr_scene_tree *tree;            /* the xdg tree: origin = geometry's top-left */
    struct wlr_scene_tree *popups;          /* its popups, in the popup layer */
    struct wlr_xdg_toplevel_decoration_v1 *deco;
    bool mapped;
    int asked_w, asked_h;                   /* the last size asked (configure) */
    struct wl_listener map, unmap, commit, destroy;
    struct wl_listener set_title, request_move, request_resize, request_maximize, request_fullscreen;
} window_t;

typedef struct popup {
    struct wlr_xdg_popup *popup;
    struct wl_listener commit, destroy;
} popup_t;

typedef struct deco {
    struct wlr_xdg_toplevel_decoration_v1 *wlr;
    struct wl_listener request_mode, destroy;
} deco_t;

typedef struct keyboard {
    struct wl_list link;
    struct wlr_keyboard *wlr;
    struct wl_listener modifiers, key, destroy;
} keyboard_t;

typedef enum { GRAB_NONE = 0, GRAB_MOVE, GRAB_RESIZE } grab_mode_t;

static struct {
    struct wl_display *display;
    struct wl_event_loop *loop;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;
    struct wlr_output_layout *layout;
    struct wlr_output *output;              /* the one output used */
    struct wlr_scene_output *scene_output;
    struct wlr_scene_rect *black;
    struct wlr_scene_tree *windows_tree;
    struct wlr_scene_buffer *picture;
    struct wlr_scene_tree *popups_tree;
    struct wlr_xdg_shell *xdg_shell;
    struct wlr_xdg_decoration_manager_v1 *deco_mgr;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *xcursor;
    struct wlr_seat *seat;
    struct wl_list windows;                 /* window_t */
    struct wl_list keyboards;               /* keyboard_t */
    struct wl_event_source *wake_source;
    int wake_fd;
    const char *socket;
    bool quit;
    int next_id;

    comp_route_t route;
    const uint16_t *frame;                  /* the frame on hand (main_native's copy) */
    bool recompose;                         /* the holes moved: compose the picture again */
    int layout_kb;                          /* the windows' keymap: FMRB_KEYMAP_LAYOUT_*, -1 none yet */
    uint8_t mods;                           /* Family mruby's modifier bits */
    int key_target[KEY_MAX + 1];            /* where each held key went down */
    int pointer_target;                     /* while a button is held: where it went down */
    bool pointer_fmrb_too;                  /* ...and the press went to Family mruby as well */
    int buttons_held;
    bool fmrb_cursor_hidden;                /* Family mruby's cursor, hidden over a window */

    /* An interactive move or resize a window asked for (xdg). */
    struct {
        grab_mode_t mode;
        int id;
        uint32_t edges;
        double cx, cy;                      /* the cursor where it began */
        int fx, fy, fw, fh;                 /* the content rectangle then (frame coordinates) */
        int nx, ny, nw, nh;                 /* where it is now */
    } grab;

    /* The last pointer position sent to a window (comp info). */
    int ptr_id;
    double ptr_sx, ptr_sy;

    /* Coalesced pointer motion for Family mruby. */
    bool motion_pending;
    int motion_x, motion_y;
    uint32_t motion_last_ms;

    /* The tools' own devices (wlr_ctl). */
    struct wlr_keyboard tool_kb;
    struct wlr_pointer tool_ptr;

    pid_t child;                            /* compositor.autostart */
    char autostart[NATIVE_MACHINE_STR];

    struct wl_listener new_output, new_input, new_toplevel, new_popup, new_decoration;
    struct wl_listener output_frame, output_request_state, output_destroy;
    struct wl_listener cursor_motion, cursor_motion_absolute, cursor_button,
                       cursor_axis, cursor_frame;
    struct wl_listener request_cursor, request_set_selection;

    /* Timings (comp stats). */
    struct {
        uint64_t since_ms;
        unsigned pictures;
        uint64_t last_picture_ms;
        uint64_t picture_gap_sum_ms;
        uint64_t picture_gap_max_ms;
        unsigned commits;
        unsigned keys;                      /* keys sent to a window */
        uint64_t key_sent_ms;               /* 0 when none is waiting for its commit */
        unsigned key_lat_n;
        uint64_t key_lat_sum_ms;
        uint64_t key_lat_max_ms;
    } stats;
} s;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static const char *renderer_name(void)
{
    if (s.renderer == NULL) return "none";
    if (wlr_renderer_is_pixman(s.renderer)) return "pixman";
    if (wlr_renderer_is_gles2(s.renderer)) return "gles2";
    if (wlr_renderer_is_vk(s.renderer)) return "vulkan";
    return "other";
}

static window_t *window_by_id(int id)
{
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (w->id == id) return w;
    }
    return NULL;
}

/* ------------------------------------------------------- the picture */

/* A frame as a wlr_buffer the renderer reads through a data pointer. Freed
 * buffers are kept for the next frames (a frame is 400 KB). ARGB: the holes
 * are transparent. */
typedef struct {
    struct wlr_buffer base;
    uint32_t *px;
    size_t cap_px;
    int w, h;
} pic_buf_t;

#define PIC_SPARE 4
static pic_buf_t *s_spare[PIC_SPARE];
static int s_n_spare = 0;

static void pic_destroy(struct wlr_buffer *wb)
{
    pic_buf_t *b = wl_container_of(wb, b, base);
    wlr_buffer_finish(wb);
    if (s_n_spare < PIC_SPARE) {
        s_spare[s_n_spare++] = b;
    } else {
        free(b->px);
        free(b);
    }
}

static bool pic_begin(struct wlr_buffer *wb, uint32_t flags, void **data, uint32_t *format,
                      size_t *stride)
{
    pic_buf_t *b = wl_container_of(wb, b, base);
    if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) return false;
    *data = b->px;
    *format = DRM_FORMAT_ARGB8888;
    *stride = (size_t)b->w * 4;
    return true;
}

static void pic_end(struct wlr_buffer *wb)
{
    (void)wb;
}

static const struct wlr_buffer_impl s_pic_impl = {
    .destroy = pic_destroy,
    .begin_data_ptr_access = pic_begin,
    .end_data_ptr_access = pic_end,
};

static pic_buf_t *pic_get(int w, int h)
{
    pic_buf_t *b = s_n_spare > 0 ? s_spare[--s_n_spare] : calloc(1, sizeof(*b));
    if (b == NULL) return NULL;
    size_t need = (size_t)w * (size_t)h;
    if (b->cap_px < need) {
        uint32_t *px = realloc(b->px, need * 4);
        if (px == NULL) {
            free(b->px);
            free(b);
            return NULL;
        }
        b->px = px;
        b->cap_px = need;
    }
    b->w = w;
    b->h = h;
    memset(&b->base, 0, sizeof(b->base));
    wlr_buffer_init(&b->base, &s_pic_impl, w, h);
    return b;
}

/* The picture from the frame on hand, holes and all. */
static void compose(void)
{
    const comp_route_t *r = &s.route;
    if (s.frame == NULL || r->fb_w <= 0 || r->fb_h <= 0) return;
    pic_buf_t *b = pic_get(r->fb_w, r->fb_h);
    if (b == NULL) return;
    comp_route_compose(r, s.frame, b->px);
    wlr_scene_buffer_set_buffer(s.picture, &b->base);
    wlr_buffer_drop(&b->base);
    s.recompose = false;
}

/* One window's scene from the route: shown or not, where, clipped to its
 * content rectangle, and asked to take that size. */
static void apply_window(window_t *w)
{
    const comp_window_t *cw = comp_route_window(&s.route, w->id);
    bool on = w->mapped && cw != NULL && cw->visible;
    wlr_scene_node_set_enabled(&w->tree->node, on);
    wlr_scene_node_set_enabled(&w->popups->node, on);
    if (cw == NULL || !cw->placed) return;
    /* The scene's xdg tree puts the window's geometry (not its surface,
     * which may reach out for a shadow) at the tree's origin. */
    wlr_scene_node_set_position(&w->tree->node, cw->x, cw->y);
    wlr_scene_node_set_position(&w->popups->node, cw->x, cw->y);
    int ww = 0, wh = 0;
    if (!comp_route_wanted_size(&s.route, w->id, &ww, &wh)) return;
    struct wlr_box g = w->toplevel->base->geometry;
    struct wlr_box clip = { .x = g.x, .y = g.y, .width = ww, .height = wh };
    wlr_scene_subsurface_tree_set_clip(&w->tree->node, &clip);
    if (w->mapped && (ww != w->asked_w || wh != w->asked_h) && ww > 0 && wh > 0) {
        w->asked_w = ww;
        w->asked_h = wh;
        wlr_xdg_toplevel_set_size(w->toplevel, ww, wh);
    }
}

/* Place the black, the picture and the windows from the route. */
static void apply_layout(void)
{
    const comp_route_t *r = &s.route;
    wlr_scene_rect_set_size(s.black, r->out_w, r->out_h);
    wlr_scene_node_set_position(&s.picture->node, r->place.x, r->place.y);
    wlr_scene_buffer_set_dest_size(s.picture, r->place.w, r->place.h);
    /* Bottom to top, as the route has them. */
    for (int i = 0; i < r->n; i++) {
        window_t *w = window_by_id(r->win[i].id);
        if (w == NULL) continue;
        wlr_scene_node_raise_to_top(&w->tree->node);
        wlr_scene_node_raise_to_top(&w->popups->node);
        apply_window(w);
    }
    s.recompose = true;
    static fmrb_present_place_t logged;
    if (memcmp(&logged, &r->place, sizeof(logged)) != 0) {
        logged = r->place;
        fprintf(stderr, "native: compositor: output %dx%d: frame %dx%d at %dx, %dx%d at (%d,%d), black around\n",
                r->out_w, r->out_h, r->fb_w, r->fb_h, r->place.scale, r->place.w, r->place.h,
                r->place.x, r->place.y);
    }
    int bw = 0, bh = 0;
    native_frame_base_size(&bw, &bh);
    comp_link_set_geometry(r->place.scale, bw, bh);
}

static void relayout(void)
{
    int ow = 0, oh = 0;
    if (s.output != NULL) wlr_output_effective_resolution(s.output, &ow, &oh);
    if (comp_route_layout(&s.route, ow, oh, s.route.fb_w, s.route.fb_h)) apply_layout();
}

/* ---------------------------------------------------------- the focus */

static void apply_focus(void)
{
    int focus = comp_route_focus(&s.route);
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (w->mapped) wlr_xdg_toplevel_set_activated(w->toplevel, w->id == focus);
    }
    window_t *fw = focus != COMP_FMRB ? window_by_id(focus) : NULL;
    if (fw == NULL || !fw->mapped) {
        wlr_seat_keyboard_notify_clear_focus(s.seat);
        return;
    }
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(s.seat);
    if (kb != NULL) {
        wlr_seat_keyboard_notify_enter(s.seat, fw->toplevel->base->surface, kb->keycodes,
                                       kb->num_keycodes, &kb->modifiers);
    } else {
        wlr_seat_keyboard_notify_enter(s.seat, fw->toplevel->base->surface, NULL, 0, NULL);
    }
}

/* Family mruby's places, stacking and focus, and its requests. */
static void take_from_fmrb(void)
{
    comp_places_t pl;
    if (comp_link_take_places(&pl)) {
        int ch = comp_route_place(&s.route, pl.p, pl.n, pl.focus);
        if (ch & (COMP_PLACE_MOVED | COMP_PLACE_ORDER)) apply_layout();
        if (ch & COMP_PLACE_FOCUS) {
            apply_focus();
            fprintf(stderr, "native: compositor: keyboard to %s%d (Family mruby's focus)\n",
                    pl.focus == COMP_FMRB ? "Family mruby " : "window ", comp_route_focus(&s.route));
        }
    }
    comp_cmd_kind_t kind;
    int id;
    while (comp_link_take_command(&kind, &id)) {
        window_t *w = window_by_id(id);
        if (kind == COMP_CMD_CLOSE && w != NULL) {
            fprintf(stderr, "native: compositor: window %d asked to close\n", id);
            wlr_xdg_toplevel_send_close(w->toplevel);
        }
    }
}

/* ------------------------------------------------------------ windows */

static void window_map(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, map);
    struct wlr_box g = w->toplevel->base->geometry;
    w->mapped = true;
    const char *app_id = w->toplevel->app_id ? w->toplevel->app_id : "";
    comp_route_window_add(&s.route, w->id, g.width, g.height, app_id);
    if (w->toplevel->title != NULL) {
        comp_route_notice(&s.route, COMP_NOTICE_TITLE, w->id, 0, 0, 0, 0, w->toplevel->title);
    }
    fprintf(stderr, "native: compositor: window %d \"%s\" %dx%d (decoration: %s)\n", w->id,
            w->toplevel->title ? w->toplevel->title : "", g.width, g.height,
            w->deco != NULL ? "server side" : "none asked");
    apply_layout();
}

static void window_unmap(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, unmap);
    w->mapped = false;
    int focus = comp_route_focus(&s.route);
    comp_route_window_remove(&s.route, w->id);
    fprintf(stderr, "native: compositor: window %d gone\n", w->id);
    if (s.grab.id == w->id) s.grab.mode = GRAB_NONE;
    if (s.pointer_target == w->id) s.pointer_target = COMP_FMRB;
    if (focus == w->id) apply_focus();
    apply_layout();
}

static void window_commit(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, commit);
    if (w->toplevel->base->initial_commit) {
        /* The client picks its own size until Family mruby places it; the
         * frame is ours. */
        wlr_xdg_toplevel_set_size(w->toplevel, 0, 0);
        if (wl_resource_get_version(w->toplevel->resource) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
            wlr_xdg_toplevel_set_wm_capabilities(w->toplevel, 0);
        }
        if (w->deco != NULL) {
            wlr_xdg_toplevel_decoration_v1_set_mode(w->deco, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }
        return;
    }
    if (!w->mapped) return;
    struct wlr_box g = w->toplevel->base->geometry;
    const comp_window_t *cw = comp_route_window(&s.route, w->id);
    if (cw != NULL && (cw->w != g.width || cw->h != g.height)) {
        comp_route_window_resize(&s.route, w->id, g.width, g.height);
        apply_window(w);
    }
    if (s.stats.key_sent_ms != 0 && w->id == comp_route_focus(&s.route)) {
        uint64_t lat = now_ms() - s.stats.key_sent_ms;
        s.stats.key_sent_ms = 0;
        s.stats.key_lat_n++;
        s.stats.key_lat_sum_ms += lat;
        if (lat > s.stats.key_lat_max_ms) s.stats.key_lat_max_ms = lat;
    }
}

static void window_set_title(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, set_title);
    if (!w->mapped || w->toplevel->title == NULL) return;
    comp_route_notice(&s.route, COMP_NOTICE_TITLE, w->id, 0, 0, 0, 0, w->toplevel->title);
}

/* Move and resize asked by the window itself (its own title bar or edges,
 * when it draws any): an interactive move or resize of the stand-in while
 * the button that started it is held. */
static void begin_grab(window_t *w, grab_mode_t mode, uint32_t edges)
{
    const comp_window_t *cw = comp_route_window(&s.route, w->id);
    if (cw == NULL || !cw->visible || s.buttons_held == 0 || s.pointer_target != w->id) return;
    s.grab.mode = mode;
    s.grab.id = w->id;
    s.grab.edges = edges;
    s.grab.cx = s.cursor->x;
    s.grab.cy = s.cursor->y;
    s.grab.fx = s.grab.nx = cw->fx;
    s.grab.fy = s.grab.ny = cw->fy;
    s.grab.fw = s.grab.nw = cw->fw;
    s.grab.fh = s.grab.nh = cw->fh;
    wlr_seat_pointer_clear_focus(s.seat);
    fprintf(stderr, "native: compositor: window %d asked to be %s\n", w->id, mode == GRAB_MOVE ? "moved" : "resized");
}

static void window_request_move(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, request_move);
    begin_grab(w, GRAB_MOVE, 0);
}

static void window_request_resize(struct wl_listener *l, void *data)
{
    struct wlr_xdg_toplevel_resize_event *ev = data;
    window_t *w = wl_container_of(l, w, request_resize);
    begin_grab(w, GRAB_RESIZE, ev->edges);
}

/* Maximize and fullscreen are not offered (L9a): the protocol still wants a
 * configure in answer, with nothing changed. */
static void window_request_state(struct wlr_xdg_toplevel *t)
{
    if (t->base->initialized) wlr_xdg_surface_schedule_configure(t->base);
}

static void window_request_maximize(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, request_maximize);
    window_request_state(w->toplevel);
}

static void window_request_fullscreen(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, request_fullscreen);
    window_request_state(w->toplevel);
}

static void window_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, destroy);
    wl_list_remove(&w->map.link);
    wl_list_remove(&w->unmap.link);
    wl_list_remove(&w->commit.link);
    wl_list_remove(&w->destroy.link);
    wl_list_remove(&w->set_title.link);
    wl_list_remove(&w->request_move.link);
    wl_list_remove(&w->request_resize.link);
    wl_list_remove(&w->request_maximize.link);
    wl_list_remove(&w->request_fullscreen.link);
    wl_list_remove(&w->link);
    wlr_scene_node_destroy(&w->popups->node);
    free(w);
}

static void new_toplevel(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_xdg_toplevel *t = data;
    window_t *w = calloc(1, sizeof(*w));
    if (w == NULL) return;
    w->id = ++s.next_id;
    w->toplevel = t;
    w->tree = wlr_scene_xdg_surface_create(s.windows_tree, t->base);
    w->tree->node.data = w;
    w->popups = wlr_scene_tree_create(s.popups_tree);
    wlr_scene_node_set_enabled(&w->tree->node, false);
    wlr_scene_node_set_enabled(&w->popups->node, false);
    /* Popups go into the popup layer, over the picture. */
    t->base->data = w->popups;
    w->map.notify = window_map;
    wl_signal_add(&t->base->surface->events.map, &w->map);
    w->unmap.notify = window_unmap;
    wl_signal_add(&t->base->surface->events.unmap, &w->unmap);
    w->commit.notify = window_commit;
    wl_signal_add(&t->base->surface->events.commit, &w->commit);
    w->destroy.notify = window_destroy;
    wl_signal_add(&t->events.destroy, &w->destroy);
    w->set_title.notify = window_set_title;
    wl_signal_add(&t->events.set_title, &w->set_title);
    w->request_move.notify = window_request_move;
    wl_signal_add(&t->events.request_move, &w->request_move);
    w->request_resize.notify = window_request_resize;
    wl_signal_add(&t->events.request_resize, &w->request_resize);
    w->request_maximize.notify = window_request_maximize;
    wl_signal_add(&t->events.request_maximize, &w->request_maximize);
    w->request_fullscreen.notify = window_request_fullscreen;
    wl_signal_add(&t->events.request_fullscreen, &w->request_fullscreen);
    wl_list_insert(&s.windows, &w->link);
}

static void popup_commit(struct wl_listener *l, void *data)
{
    (void)data;
    popup_t *p = wl_container_of(l, p, commit);
    if (p->popup->base->initial_commit) wlr_xdg_surface_schedule_configure(p->popup->base);
}

static void popup_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    popup_t *p = wl_container_of(l, p, destroy);
    wl_list_remove(&p->commit.link);
    wl_list_remove(&p->destroy.link);
    free(p);
}

static void new_popup(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_xdg_popup *xp = data;
    struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xp->parent);
    if (parent == NULL || parent->data == NULL) return;
    popup_t *p = calloc(1, sizeof(*p));
    if (p == NULL) return;
    p->popup = xp;
    xp->base->data = wlr_scene_xdg_surface_create(parent->data, xp->base);
    p->commit.notify = popup_commit;
    wl_signal_add(&xp->base->surface->events.commit, &p->commit);
    p->destroy.notify = popup_destroy;
    wl_signal_add(&xp->events.destroy, &p->destroy);
}

/* xdg-decoration: every window is told the frame is the server's. */
static void deco_request_mode(struct wl_listener *l, void *data)
{
    (void)data;
    deco_t *d = wl_container_of(l, d, request_mode);
    if (d->wlr->toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(d->wlr, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
}

static void deco_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    deco_t *d = wl_container_of(l, d, destroy);
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (w->deco == d->wlr) w->deco = NULL;
    }
    wl_list_remove(&d->request_mode.link);
    wl_list_remove(&d->destroy.link);
    free(d);
}

static void new_decoration(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_xdg_toplevel_decoration_v1 *wd = data;
    deco_t *d = calloc(1, sizeof(*d));
    if (d == NULL) return;
    d->wlr = wd;
    d->request_mode.notify = deco_request_mode;
    wl_signal_add(&wd->events.request_mode, &d->request_mode);
    d->destroy.notify = deco_destroy;
    wl_signal_add(&wd->events.destroy, &d->destroy);
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (w->toplevel == wd->toplevel) w->deco = wd;
    }
    if (wd->toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(wd, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
}

/* ----------------------------------------------------------- keyboard */

static void send_fmrb_key(uint32_t code, bool down)
{
    uint8_t usage = fmrb_linux_key_to_hid(code);
    if (usage == 0) return;
    native_input_push(down ? NATIVE_INPUT_KEY_DOWN : NATIVE_INPUT_KEY_UP, usage, s.mods, 0, 0);
}

/* Keys that stay Family mruby's whoever has the keyboard: the window
 * switching hotkeys (host_task.c), so a Linux window cannot trap them. */
static bool fmrb_hotkey(uint32_t code)
{
    bool ctrl = (s.mods & (FMRB_KEYMAP_MOD_LCTRL | FMRB_KEYMAP_MOD_RCTRL)) != 0;
    return ctrl && (code == KEY_TAB || code == KEY_COMMA);
}

static void keyboard_key(struct wl_listener *l, void *data)
{
    keyboard_t *k = wl_container_of(l, k, key);
    struct wlr_keyboard_key_event *ev = data;
    uint32_t code = ev->keycode;
    bool down = ev->state == WL_KEYBOARD_KEY_STATE_PRESSED;
    /* Family mruby's modifier bits follow every key, wherever it goes, so a
     * hotkey sent to Family mruby carries the modifiers held for a window. */
    uint8_t bit = fmrb_linux_key_mod_bit(code);
    if (bit != 0) {
        if (down) s.mods |= bit;
        else s.mods &= (uint8_t)~bit;
    }
    /* A key goes up where it went down, whoever has the keyboard now. */
    int target = comp_route_focus(&s.route);
    if (down && fmrb_hotkey(code)) target = COMP_FMRB;
    if (code <= KEY_MAX) {
        if (down) s.key_target[code] = target;
        else target = s.key_target[code];
    }
    if (target == COMP_FMRB || window_by_id(target) == NULL) {
        send_fmrb_key(code, down);
        return;
    }
    wlr_seat_set_keyboard(s.seat, k->wlr);
    wlr_seat_keyboard_notify_key(s.seat, ev->time_msec, code, ev->state);
    if (down) {
        s.stats.keys++;
        if (s.stats.key_sent_ms == 0) s.stats.key_sent_ms = now_ms();
    }
}

static void keyboard_modifiers(struct wl_listener *l, void *data)
{
    (void)data;
    keyboard_t *k = wl_container_of(l, k, modifiers);
    if (comp_route_focus(&s.route) == COMP_FMRB) return;
    wlr_seat_set_keyboard(s.seat, k->wlr);
    wlr_seat_keyboard_notify_modifiers(s.seat, &k->wlr->modifiers);
}

static void keyboard_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    keyboard_t *k = wl_container_of(l, k, destroy);
    wl_list_remove(&k->modifiers.link);
    wl_list_remove(&k->key.link);
    wl_list_remove(&k->destroy.link);
    wl_list_remove(&k->link);
    free(k);
}

/* The windows' keymap: the layout Family mruby's keyboard_layout names
 * (the keys themselves are Linux key codes whatever the layout; Family
 * mruby reads them with its own tables). */
static void set_keymap(struct wlr_keyboard *wkb, int layout)
{
    struct xkb_rule_names names = { 0 };
    names.layout = layout == FMRB_KEYMAP_LAYOUT_JP ? "jp" : "us";
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *map = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (map != NULL) {
        wlr_keyboard_set_keymap(wkb, map);
        xkb_keymap_unref(map);
    }
    xkb_context_unref(ctx);
}

static void follow_layout(void)
{
    int layout = (int)fmrb_keymap_get_layout();
    if (layout == s.layout_kb) return;
    s.layout_kb = layout;
    keyboard_t *k;
    wl_list_for_each(k, &s.keyboards, link) set_keymap(k->wlr, layout);
    fprintf(stderr, "native: compositor: the windows' keymap is %s (keyboard_layout)\n",
            layout == FMRB_KEYMAP_LAYOUT_JP ? "jp" : "us");
}

static void add_keyboard(struct wlr_keyboard *wkb)
{
    keyboard_t *k = calloc(1, sizeof(*k));
    if (k == NULL) return;
    k->wlr = wkb;
    set_keymap(wkb, s.layout_kb < 0 ? (int)fmrb_keymap_get_layout() : s.layout_kb);
    wlr_keyboard_set_repeat_info(wkb, 25, 600);
    k->modifiers.notify = keyboard_modifiers;
    wl_signal_add(&wkb->events.modifiers, &k->modifiers);
    k->key.notify = keyboard_key;
    wl_signal_add(&wkb->events.key, &k->key);
    k->destroy.notify = keyboard_destroy;
    wl_signal_add(&wkb->base.events.destroy, &k->destroy);
    wl_list_insert(&s.keyboards, &k->link);
    wlr_seat_set_keyboard(s.seat, wkb);
}

static void new_input(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_input_device *dev = data;
    if (dev->type == WLR_INPUT_DEVICE_KEYBOARD) {
        add_keyboard(wlr_keyboard_from_input_device(dev));
    } else if (dev->type == WLR_INPUT_DEVICE_POINTER) {
        wlr_cursor_attach_input_device(s.cursor, dev);
    }
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(s.seat, caps);
}

/* ------------------------------------------------------------ pointer */

static void flush_motion(bool force)
{
    if (!s.motion_pending) return;
    uint32_t t = (uint32_t)now_ms();
    if (!force && t - s.motion_last_ms < MOTION_INTERVAL_MS) return;
    native_input_push(NATIVE_INPUT_MOUSE_MOVE, (uint32_t)s.motion_x, (uint32_t)s.motion_y, 0, 0);
    s.motion_pending = false;
    s.motion_last_ms = t;
}

/* Family mruby's cursor: hidden while the pointer is over a Linux window
 * (wlroots' shows there), back when it leaves. */
static void fmrb_cursor(bool show)
{
    if (show == !s.fmrb_cursor_hidden) return;
    s.fmrb_cursor_hidden = !show;
    native_input_push(NATIVE_INPUT_CURSOR, show ? 1u : 0u, 0, 0, 0);
}

/* A popup of a shown window under the point: its window's id, the surface
 * and surface-local coordinates. */
static int popup_at(double x, double y, struct wlr_surface **surf, double *sx, double *sy)
{
    const comp_route_t *r = &s.route;
    for (int i = r->n - 1; i >= 0; i--) {
        if (!r->win[i].visible) continue;
        window_t *w = window_by_id(r->win[i].id);
        if (w == NULL) continue;
        struct wlr_scene_node *node = wlr_scene_node_at(&w->popups->node, x, y, sx, sy);
        if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) continue;
        struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
        if (ss == NULL) continue;
        *surf = ss->surface;
        return w->id;
    }
    return COMP_FMRB;
}

/* The surface of window id under the point (its subsurfaces included), with
 * surface-local coordinates; its main surface when the point is in its
 * rectangle but on no surface of it. */
static struct wlr_surface *surface_at(int id, double *sx, double *sy)
{
    window_t *w = window_by_id(id);
    if (w == NULL) return NULL;
    struct wlr_scene_node *node = wlr_scene_node_at(&w->tree->node, s.cursor->x, s.cursor->y, sx, sy);
    if (node != NULL && node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
        if (ss != NULL) return ss->surface;
    }
    const comp_window_t *cw = comp_route_window(&s.route, id);
    if (cw == NULL) return NULL;
    struct wlr_box g = w->toplevel->base->geometry;
    *sx = s.cursor->x - cw->x + g.x;
    *sy = s.cursor->y - cw->y + g.y;
    return w->toplevel->base->surface;
}

typedef struct {
    comp_hit_t hit;
    struct wlr_surface *surf;               /* a window: the surface */
    double sx, sy;
    bool popup;                             /* over one of its popups */
} ptr_hit_t;

/* What the pointer is over: a popup, a hole, or Family mruby. While a
 * button is held, the press's target keeps it. */
static ptr_hit_t pointer_hit(void)
{
    ptr_hit_t p = { 0 };
    double x = s.cursor->x, y = s.cursor->y;
    int pid = popup_at(x, y, &p.surf, &p.sx, &p.sy);
    if (pid != COMP_FMRB) {
        p.hit.target = pid;
        p.popup = true;
    } else {
        p.hit = comp_route_at(&s.route, (int)x, (int)y);
    }
    if (s.buttons_held > 0 && p.hit.target != s.pointer_target) {
        p.hit.target = s.pointer_target;
        p.popup = false;
        p.surf = NULL;
        if (p.hit.target == COMP_FMRB) {
            /* Clamped into the frame, as a drag out of the picture is. */
            fmrb_present_to_frame(&s.route.place, s.route.fb_w, s.route.fb_h, (int)x, (int)y,
                                  &p.hit.x, &p.hit.y);
        }
    }
    if (p.hit.target != COMP_FMRB && p.surf == NULL) p.surf = surface_at(p.hit.target, &p.sx, &p.sy);
    return p;
}

/* Where the pointer is in frame coordinates, clamped. */
static void pointer_frame(int *fx, int *fy)
{
    fmrb_present_to_frame(&s.route.place, s.route.fb_w, s.route.fb_h, (int)s.cursor->x, (int)s.cursor->y,
                          fx, fy);
}

static void grab_motion(void)
{
    int sc = s.route.place.scale > 0 ? s.route.place.scale : 1;
    int dx = (int)floor((s.cursor->x - s.grab.cx) / sc);
    int dy = (int)floor((s.cursor->y - s.grab.cy) / sc);
    if (s.grab.mode == GRAB_MOVE) {
        int nx = s.grab.fx + dx, ny = s.grab.fy + dy;
        if (nx != s.grab.nx || ny != s.grab.ny) {
            s.grab.nx = nx;
            s.grab.ny = ny;
            comp_route_notice(&s.route, COMP_NOTICE_MOVE, s.grab.id, nx, ny, 0, 0, NULL);
        }
        return;
    }
    int x = s.grab.fx, y = s.grab.fy, w = s.grab.fw, h = s.grab.fh;
    if (s.grab.edges & WLR_EDGE_RIGHT) w += dx;
    if (s.grab.edges & WLR_EDGE_BOTTOM) h += dy;
    if (s.grab.edges & WLR_EDGE_LEFT) {
        x += dx;
        w -= dx;
    }
    if (s.grab.edges & WLR_EDGE_TOP) {
        y += dy;
        h -= dy;
    }
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    s.grab.nx = x;
    s.grab.ny = y;
    s.grab.nw = w;
    s.grab.nh = h;
}

static void process_motion(uint32_t time_msec)
{
    if (s.grab.mode != GRAB_NONE) {
        grab_motion();
        return;
    }
    ptr_hit_t p = pointer_hit();
    if (p.hit.target == COMP_FMRB) {
        wlr_seat_pointer_clear_focus(s.seat);
        wlr_cursor_unset_image(s.cursor);
        s.motion_x = p.hit.x;
        s.motion_y = p.hit.y;
        s.motion_pending = true;
        fmrb_cursor(true);
        flush_motion(false);
        return;
    }
    fmrb_cursor(false);
    if (p.surf == NULL) {
        wlr_seat_pointer_clear_focus(s.seat);
        return;
    }
    if (s.seat->pointer_state.focused_surface != p.surf) {
        wlr_cursor_set_xcursor(s.cursor, s.xcursor, "default");
    }
    wlr_seat_pointer_notify_enter(s.seat, p.surf, p.sx, p.sy);
    wlr_seat_pointer_notify_motion(s.seat, time_msec, p.sx, p.sy);
    s.ptr_id = p.hit.target;
    s.ptr_sx = p.sx;
    s.ptr_sy = p.sy;
}

static void cursor_motion(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_pointer_motion_event *ev = data;
    wlr_cursor_move(s.cursor, &ev->pointer->base, ev->delta_x, ev->delta_y);
    process_motion(ev->time_msec);
}

static void cursor_motion_absolute(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_pointer_motion_absolute_event *ev = data;
    wlr_cursor_warp_absolute(s.cursor, &ev->pointer->base, ev->x, ev->y);
    process_motion(ev->time_msec);
}

static int fmrb_button(uint32_t linux_button)
{
    switch (linux_button) {
    case BTN_LEFT: return 1;
    case BTN_MIDDLE: return 2;
    case BTN_RIGHT: return 3;
    default: return 0;
    }
}

static void fmrb_press(int fx, int fy, int b, bool down)
{
    s.motion_x = fx;
    s.motion_y = fy;
    s.motion_pending = true;
    flush_motion(true);
    native_input_push(NATIVE_INPUT_MOUSE_BUTTON, (uint32_t)fx, (uint32_t)fy, (uint32_t)b, down ? 1 : 0);
}

static void end_grab(void)
{
    if (s.grab.mode == GRAB_RESIZE &&
        (s.grab.nw != s.grab.fw || s.grab.nh != s.grab.fh || s.grab.nx != s.grab.fx || s.grab.ny != s.grab.fy)) {
        comp_route_notice(&s.route, COMP_NOTICE_RESIZE, s.grab.id, s.grab.nx, s.grab.ny, s.grab.nw, s.grab.nh,
                          NULL);
    }
    s.grab.mode = GRAB_NONE;
}

static void cursor_button(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_pointer_button_event *ev = data;
    bool down = ev->state == WL_POINTER_BUTTON_STATE_PRESSED;
    int b = fmrb_button(ev->button);
    ptr_hit_t p;
    if (down && s.buttons_held == 0) {
        p = pointer_hit();
        s.pointer_fmrb_too = false;
        if (p.popup) {
            /* A popup is the window's own business: no focus change. */
            s.pointer_target = p.hit.target;
        } else {
            bool changed = false;
            comp_hit_t h = comp_route_press(&s.route, (int)s.cursor->x, (int)s.cursor->y, &changed);
            s.pointer_target = h.target;
            if (changed) {
                apply_focus();
                fprintf(stderr, "native: compositor: keyboard to %s%d\n",
                        h.target == COMP_FMRB ? "Family mruby " : "window ", h.target);
            }
            if (h.target != COMP_FMRB) {
                apply_layout();
                /* Family mruby gets the press too, at the frame pixel: its
                 * window manager raises and focuses the stand-in. */
                if (b != 0) {
                    fmrb_press(h.fx, h.fy, b, true);
                    s.pointer_fmrb_too = true;
                }
            }
        }
    } else {
        p = pointer_hit();
    }
    s.buttons_held += down ? 1 : -1;
    if (s.buttons_held < 0) s.buttons_held = 0;

    if (p.hit.target == COMP_FMRB) {
        if (b == 0) return;
        fmrb_press(p.hit.x, p.hit.y, b, down);
        return;
    }
    if (!down && s.pointer_fmrb_too && b != 0) {
        int fx = 0, fy = 0;
        pointer_frame(&fx, &fy);
        fmrb_press(fx, fy, b, false);
        if (s.buttons_held == 0) s.pointer_fmrb_too = false;
    }
    if (s.grab.mode != GRAB_NONE && !down) {
        end_grab();
    }
    process_motion(ev->time_msec);
    wlr_seat_pointer_notify_button(s.seat, ev->time_msec, ev->button, ev->state);
}

static void cursor_axis(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_pointer_axis_event *ev = data;
    ptr_hit_t p = pointer_hit();
    if (p.hit.target == COMP_FMRB) {
        if (ev->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
        /* Wayland's positive is towards the user; the system's is away. */
        int notches = ev->delta_discrete != 0 ? -ev->delta_discrete / WLR_POINTER_AXIS_DISCRETE_STEP
                                              : (ev->delta > 0 ? -1 : (ev->delta < 0 ? 1 : 0));
        if (notches == 0) return;
        flush_motion(true);
        native_input_push(NATIVE_INPUT_MOUSE_WHEEL, (uint32_t)p.hit.x, (uint32_t)p.hit.y,
                          (uint32_t)(int32_t)notches, 0);
        return;
    }
    wlr_seat_pointer_notify_axis(s.seat, ev->time_msec, ev->orientation, ev->delta,
                                 ev->delta_discrete, ev->source, ev->relative_direction);
}

static void cursor_frame(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    wlr_seat_pointer_notify_frame(s.seat);
}

static void request_cursor(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_seat_pointer_request_set_cursor_event *ev = data;
    if (s.seat->pointer_state.focused_client == ev->seat_client) {
        wlr_cursor_set_surface(s.cursor, ev->surface, ev->hotspot_x, ev->hotspot_y);
    }
}

static void request_set_selection(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_seat_request_set_selection_event *ev = data;
    wlr_seat_set_selection(s.seat, ev->source, ev->serial);
}

/* ------------------------------------------------------------- output */

static void output_frame(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    if (s.scene_output == NULL) return;
    if (wlr_scene_output_commit(s.scene_output, NULL)) s.stats.commits++;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    wlr_scene_output_send_frame_done(s.scene_output, &ts);
}

static void output_request_state(struct wl_listener *l, void *data)
{
    (void)l;
    const struct wlr_output_event_request_state *ev = data;
    wlr_output_commit_state(ev->output, ev->state);
    relayout();
}

static void output_destroy(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    wl_list_remove(&s.output_frame.link);
    wl_list_remove(&s.output_request_state.link);
    wl_list_remove(&s.output_destroy.link);
    s.output = NULL;
    s.scene_output = NULL;
    /* The nested window closed (or the display went): the session ends. */
    fprintf(stderr, "native: compositor: the output went away; quitting\n");
    s.quit = true;
}

static void new_output(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_output *out = data;
    if (s.output != NULL) {
        fprintf(stderr, "native: compositor: output %s not used (one output in L8)\n", out->name);
        return;
    }
    wlr_output_init_render(out, s.allocator, s.renderer);
    struct wlr_output_state st;
    wlr_output_state_init(&st);
    wlr_output_state_set_enabled(&st, true);
    struct wlr_output_mode *mode = wlr_output_preferred_mode(out);
    if (mode != NULL) wlr_output_state_set_mode(&st, mode);
    wlr_output_commit_state(out, &st);
    wlr_output_state_finish(&st);

    s.output = out;
    s.output_frame.notify = output_frame;
    wl_signal_add(&out->events.frame, &s.output_frame);
    s.output_request_state.notify = output_request_state;
    wl_signal_add(&out->events.request_state, &s.output_request_state);
    s.output_destroy.notify = output_destroy;
    wl_signal_add(&out->events.destroy, &s.output_destroy);

    struct wlr_output_layout_output *lo = wlr_output_layout_add_auto(s.layout, out);
    s.scene_output = wlr_scene_output_create(s.scene, out);
    wlr_scene_output_layout_add_output(s.scene_layout, lo, s.scene_output);
    fprintf(stderr, "native: compositor: output %s %dx%d\n", out->name, out->width, out->height);
    relayout();
}

/* ---------------------------------------------------- the tools' devices */

static const struct wlr_keyboard_impl s_tool_kb_impl = { .name = "fmrb-tools-keyboard" };
static const struct wlr_pointer_impl s_tool_ptr_impl = { .name = "fmrb-tools-pointer" };

static void tool_devices_init(void)
{
    wlr_keyboard_init(&s.tool_kb, &s_tool_kb_impl, "fmrb-tools-keyboard");
    add_keyboard(&s.tool_kb);
    wlr_pointer_init(&s.tool_ptr, &s_tool_ptr_impl, "fmrb-tools-pointer");
    wlr_cursor_attach_input_device(s.cursor, &s.tool_ptr.base);
    wlr_seat_set_capabilities(s.seat, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
}

static void tool_key(unsigned code, bool down)
{
    struct wlr_keyboard_key_event ev = {
        .time_msec = (uint32_t)now_ms(),
        .keycode = code,
        .update_state = true,
        .state = down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED,
    };
    wlr_keyboard_notify_key(&s.tool_kb, &ev);
}

static void tool_move(int x, int y)
{
    if (s.route.out_w <= 0 || s.route.out_h <= 0) return;
    struct wlr_pointer_motion_absolute_event ev = {
        .pointer = &s.tool_ptr,
        .time_msec = (uint32_t)now_ms(),
        .x = ((double)x + 0.5) / s.route.out_w,
        .y = ((double)y + 0.5) / s.route.out_h,
    };
    wl_signal_emit_mutable(&s.tool_ptr.events.motion_absolute, &ev);
    wl_signal_emit_mutable(&s.tool_ptr.events.frame, &s.tool_ptr);
}

static void tool_button(int b, bool down)
{
    static const uint32_t map[] = { 0, BTN_LEFT, BTN_MIDDLE, BTN_RIGHT };
    if (b < 1 || b > 3) return;
    struct wlr_pointer_button_event ev = {
        .pointer = &s.tool_ptr,
        .time_msec = (uint32_t)now_ms(),
        .button = map[b],
        .state = down ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED,
    };
    wlr_pointer_notify_button(&s.tool_ptr, &ev);
    wl_signal_emit_mutable(&s.tool_ptr.events.frame, &s.tool_ptr);
}

static void tool_wheel(int notches)
{
    struct wlr_pointer_axis_event ev = {
        .pointer = &s.tool_ptr,
        .time_msec = (uint32_t)now_ms(),
        .source = WL_POINTER_AXIS_SOURCE_WHEEL,
        .orientation = WL_POINTER_AXIS_VERTICAL_SCROLL,
        .relative_direction = WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL,
        .delta = -15.0 * notches,
        .delta_discrete = -WLR_POINTER_AXIS_DISCRETE_STEP * notches,
    };
    wl_signal_emit_mutable(&s.tool_ptr.events.axis, &ev);
    wl_signal_emit_mutable(&s.tool_ptr.events.frame, &s.tool_ptr);
}

/*
 * The control socket's "comp ..." (tools/fmrb_native.rb comp):
 *
 *   comp info            "OK out=WxH frame=WxH scale=S at=X,Y focus=F
 *                        windows=ID:WxH+X+Y/FXxFY+FWxFH/V,... ptr=ID:SX,SY
 *                        cursor=fmrb|window keymap=us|jp socket=NAME
 *                        renderer=R": each window's size and place on the
 *                        output, its content rectangle in frame coordinates
 *                        and whether it shows (V 1/0), bottom to top; the
 *                        last pointer position sent to a window
 *                        (surface-local, output pixels)
 *   comp stats [reset]   pictures (count, mean and longest gap), output
 *                        commits, keys sent to a window and the time from
 *                        each to the window's next commit, since the last
 *                        reset
 *   comp key CODE 1|0    a Linux key code down / up, through the seat
 *   comp move X Y        the pointer to output coordinates
 *   comp button B 1|0    1 left, 2 middle, 3 right
 *   comp click X Y       move, then left down and up
 *   comp wheel N         notches, + away from the user
 *
 * The tools' keyboard and pointer go through the same handlers (focus,
 * routing) as the backend's devices.
 */
static bool wlr_ctl(const char *req, char *out, size_t cap)
{
    if (strncmp(req, "comp ", 5) != 0 && strcmp(req, "comp") != 0) return false;
    const char *a = req + 4;
    while (*a == ' ') a++;
    int x = 0, y = 0;
    unsigned code = 0;
    if (strcmp(a, "info") == 0 || a[0] == '\0') {
        const comp_route_t *r = &s.route;
        int n = snprintf(out, cap, "OK out=%dx%d frame=%dx%d scale=%d at=%d,%d focus=%d windows=",
                         r->out_w, r->out_h, r->fb_w, r->fb_h, r->place.scale, r->place.x, r->place.y,
                         r->focus);
        for (int i = 0; i < r->n && n > 0 && (size_t)n < cap; i++) {
            const comp_window_t *w = &r->win[i];
            n += snprintf(out + n, cap - (size_t)n, "%s%d:%dx%d+%d+%d/%dx%d+%dx%d/%d", i ? "," : "", w->id, w->w,
                          w->h, w->x, w->y, w->fx, w->fy, w->fw, w->fh, w->visible ? 1 : 0);
        }
        if (n > 0 && (size_t)n < cap) {
            snprintf(out + n, cap - (size_t)n, " ptr=%d:%.2f,%.2f cursor=%s keymap=%s socket=%s renderer=%s\n",
                     s.ptr_id, s.ptr_sx, s.ptr_sy, s.fmrb_cursor_hidden ? "window" : "fmrb",
                     s.layout_kb == FMRB_KEYMAP_LAYOUT_JP ? "jp" : "us", s.socket, renderer_name());
        }
        return true;
    }
    if (strncmp(a, "stats", 5) == 0) {
        uint64_t t = now_ms();
        double span = (double)(t - s.stats.since_ms) / 1000.0;
        unsigned gaps = s.stats.pictures > 1 ? s.stats.pictures - 1 : 0;
        snprintf(out, cap,
                 "OK span_s=%.1f pictures=%u gap_mean_ms=%.1f gap_max_ms=%llu commits=%u keys=%u "
                 "key_commit_n=%u key_commit_mean_ms=%.1f key_commit_max_ms=%llu\n",
                 span, s.stats.pictures,
                 gaps ? (double)s.stats.picture_gap_sum_ms / gaps : 0.0,
                 (unsigned long long)s.stats.picture_gap_max_ms, s.stats.commits, s.stats.keys,
                 s.stats.key_lat_n, s.stats.key_lat_n ? (double)s.stats.key_lat_sum_ms / s.stats.key_lat_n : 0.0,
                 (unsigned long long)s.stats.key_lat_max_ms);
        if (strcmp(a, "stats reset") == 0) {
            memset(&s.stats, 0, sizeof(s.stats));
            s.stats.since_ms = t;
        }
        return true;
    }
    int b = 0, st = 0, n = 0;
    if (sscanf(a, "key %u %d", &code, &st) == 2) {
        tool_key(code, st != 0);
    } else if (sscanf(a, "move %d %d", &x, &y) == 2) {
        tool_move(x, y);
    } else if (sscanf(a, "button %d %d", &b, &st) == 2) {
        tool_button(b, st != 0);
    } else if (sscanf(a, "click %d %d", &x, &y) == 2) {
        tool_move(x, y);
        tool_button(1, true);
        tool_button(1, false);
    } else if (sscanf(a, "wheel %d", &n) == 1) {
        tool_wheel(n);
    } else {
        snprintf(out, cap, "ERR comp: info, stats [reset], key CODE 1|0, move X Y, button B 1|0, click X Y, wheel N\n");
        return true;
    }
    wl_display_flush_clients(s.display);
    snprintf(out, cap, "OK\n");
    return true;
}

/* ----------------------------------------------------- the autostart */

static void start_child(void)
{
    if (s.autostart[0] == '\0') return;
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "native: compositor: autostart: fork: %s\n", strerror(errno));
        return;
    }
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", s.autostart, (char *)NULL);
        _exit(127);
    }
    s.child = pid;
    fprintf(stderr, "native: compositor: started \"%s\" (pid %d) on %s\n", s.autostart, (int)pid, s.socket);
}

static void reap_child(void)
{
    if (s.child <= 0) return;
    int st = 0;
    if (waitpid(s.child, &st, WNOHANG) == s.child) {
        fprintf(stderr, "native: compositor: \"%s\" ended (%d)\n", s.autostart,
                WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st));
        s.child = 0;
    }
}

static void stop_child(void)
{
    if (s.child <= 0) return;
    kill(-s.child, SIGTERM);
    for (int i = 0; i < 50; i++) {
        if (waitpid(s.child, NULL, WNOHANG) == s.child) {
            fprintf(stderr, "native: compositor: \"%s\" stopped with the program\n", s.autostart);
            s.child = 0;
            return;
        }
        usleep(10 * 1000);
    }
    kill(-s.child, SIGKILL);
    waitpid(s.child, NULL, 0);
    fprintf(stderr, "native: compositor: \"%s\" killed with the program\n", s.autostart);
    s.child = 0;
}

/* ------------------------------------------------- the output interface */

static int on_wake(int fd, uint32_t mask, void *data)
{
    (void)mask;
    (void)data;
    uint64_t v;
    ssize_t n = read(fd, &v, sizeof(v));
    (void)n;
    return 0;
}

static bool wlr_open(const native_machine_t *m, char *why, size_t why_len)
{
    memset(&s, 0, sizeof(s));
    s.wake_fd = -1;
    s.layout_kb = -1;
    comp_route_init(&s.route);
    wl_list_init(&s.windows);
    wl_list_init(&s.keyboards);
    snprintf(s.autostart, sizeof(s.autostart), "%s", m->compositor.autostart);
    s.stats.since_ms = now_ms();
    if (getenv("XDG_RUNTIME_DIR") == NULL) {
        snprintf(why, why_len, "XDG_RUNTIME_DIR is not set (the compositor's socket goes there)");
        return false;
    }
    wlr_log_init(getenv("FMRB_WLR_DEBUG") ? WLR_DEBUG : WLR_ERROR, NULL);

    s.display = wl_display_create();
    s.loop = wl_display_get_event_loop(s.display);
    /* Nested in another compositor when WAYLAND_DISPLAY or DISPLAY is set,
     * DRM/KMS on a console; WLR_BACKENDS chooses (headless for the tests). */
    s.backend = wlr_backend_autocreate(s.loop, NULL);
    if (s.backend == NULL) {
        snprintf(why, why_len, "no wlroots backend (WAYLAND_DISPLAY=%s, WLR_BACKENDS=%s; FMRB_WLR_DEBUG=1 says why)",
                 getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "",
                 getenv("WLR_BACKENDS") ? getenv("WLR_BACKENDS") : "");
        return false;
    }
    s.renderer = wlr_renderer_autocreate(s.backend);
    if (s.renderer == NULL) {
        snprintf(why, why_len, "no wlroots renderer (WLR_RENDERER=pixman works without a GPU)");
        return false;
    }
    wlr_renderer_init_wl_display(s.renderer, s.display);
    s.allocator = wlr_allocator_autocreate(s.backend, s.renderer);
    if (s.allocator == NULL) {
        snprintf(why, why_len, "no wlroots allocator");
        return false;
    }
    wlr_compositor_create(s.display, 5, s.renderer);
    wlr_subcompositor_create(s.display);
    wlr_data_device_manager_create(s.display);

    s.layout = wlr_output_layout_create(s.display);
    s.scene = wlr_scene_create();
    s.scene_layout = wlr_scene_attach_output_layout(s.scene, s.layout);
    static const float black[4] = { 0, 0, 0, 1 };
    s.black = wlr_scene_rect_create(&s.scene->tree, 0, 0, black);
    /* The Linux windows under the picture, their popups over it. */
    s.windows_tree = wlr_scene_tree_create(&s.scene->tree);
    s.picture = wlr_scene_buffer_create(&s.scene->tree, NULL);
    wlr_scene_buffer_set_filter_mode(s.picture, WLR_SCALE_FILTER_NEAREST);
    s.popups_tree = wlr_scene_tree_create(&s.scene->tree);

    s.new_output.notify = new_output;
    wl_signal_add(&s.backend->events.new_output, &s.new_output);
    /* Version 5: the window is told which window-manager features there
     * are (none of maximize, fullscreen, minimize, window menu: L9a). */
    s.xdg_shell = wlr_xdg_shell_create(s.display, 5);
    s.new_toplevel.notify = new_toplevel;
    wl_signal_add(&s.xdg_shell->events.new_toplevel, &s.new_toplevel);
    s.new_popup.notify = new_popup;
    wl_signal_add(&s.xdg_shell->events.new_popup, &s.new_popup);
    s.deco_mgr = wlr_xdg_decoration_manager_v1_create(s.display);
    s.new_decoration.notify = new_decoration;
    wl_signal_add(&s.deco_mgr->events.new_toplevel_decoration, &s.new_decoration);

    s.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(s.cursor, s.layout);
    s.xcursor = wlr_xcursor_manager_create(NULL, 24);
    s.cursor_motion.notify = cursor_motion;
    wl_signal_add(&s.cursor->events.motion, &s.cursor_motion);
    s.cursor_motion_absolute.notify = cursor_motion_absolute;
    wl_signal_add(&s.cursor->events.motion_absolute, &s.cursor_motion_absolute);
    s.cursor_button.notify = cursor_button;
    wl_signal_add(&s.cursor->events.button, &s.cursor_button);
    s.cursor_axis.notify = cursor_axis;
    wl_signal_add(&s.cursor->events.axis, &s.cursor_axis);
    s.cursor_frame.notify = cursor_frame;
    wl_signal_add(&s.cursor->events.frame, &s.cursor_frame);

    s.new_input.notify = new_input;
    wl_signal_add(&s.backend->events.new_input, &s.new_input);
    s.seat = wlr_seat_create(s.display, "seat0");
    s.request_cursor.notify = request_cursor;
    wl_signal_add(&s.seat->events.request_set_cursor, &s.request_cursor);
    s.request_set_selection.notify = request_set_selection;
    wl_signal_add(&s.seat->events.request_set_selection, &s.request_set_selection);
    tool_devices_init();

    wlr_screencopy_manager_v1_create(s.display);
    wlr_xdg_output_manager_v1_create(s.display, s.layout);

    s.socket = wl_display_add_socket_auto(s.display);
    if (s.socket == NULL) {
        snprintf(why, why_len, "cannot make a Wayland socket in %s", getenv("XDG_RUNTIME_DIR"));
        return false;
    }
    if (!wlr_backend_start(s.backend)) {
        snprintf(why, why_len, "the wlroots backend did not start");
        return false;
    }
    /* The parent compositor (nested) is connected by now: from here on the
     * program's own children, and the Shell's "!" programs, see this one. */
    setenv("WAYLAND_DISPLAY", s.socket, 1);

    s.wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (s.wake_fd >= 0) {
        s.wake_source = wl_event_loop_add_fd(s.loop, s.wake_fd, WL_EVENT_READABLE, on_wake, NULL);
        native_frame_set_wake_fd(s.wake_fd);
    }
    comp_link_attach(&s.route, s.wake_fd);
    fprintf(stderr, "native: compositor: wlroots %s on WAYLAND_DISPLAY=%s (backends %s, renderer %s)\n",
            WLR_VERSION_STR, s.socket, getenv("WLR_BACKENDS") ? getenv("WLR_BACKENDS") : "auto",
            renderer_name());
    start_child();
    return true;
}

static void take_base_size(void)
{
    if (s.route.base_w > 0) return;
    int bw = 0, bh = 0;
    native_frame_base_size(&bw, &bh);
    if (bw <= 0 || bh <= 0) return;
    comp_route_set_base(&s.route, bw, bh);
    comp_link_set_geometry(s.route.place.scale, bw, bh);
}

static bool wlr_pump(int wait_ms)
{
    wl_display_flush_clients(s.display);
    wl_event_loop_dispatch(s.loop, wait_ms);
    take_base_size();
    follow_layout();
    take_from_fmrb();
    wl_display_flush_clients(s.display);
    flush_motion(false);
    reap_child();
    return s.quit;
}

static void wlr_present(const uint16_t *px, int w, int h, bool changed)
{
    if (s.picture == NULL) return;
    s.frame = px;
    comp_route_set_frame(&s.route, px);
    if (w != s.route.fb_w || h != s.route.fb_h) {
        int ow = s.route.out_w, oh = s.route.out_h;
        if (comp_route_layout(&s.route, ow, oh, w, h)) apply_layout();
    }
    if (!changed && !s.recompose) return;
    compose();
    if (!changed) return;
    uint64_t t = now_ms();
    if (s.stats.pictures > 0) {
        uint64_t gap = t - s.stats.last_picture_ms;
        s.stats.picture_gap_sum_ms += gap;
        if (gap > s.stats.picture_gap_max_ms) s.stats.picture_gap_max_ms = gap;
    }
    s.stats.last_picture_ms = t;
    s.stats.pictures++;
}

static int window_count(void)
{
    int n = 0;
    window_t *w;
    wl_list_for_each(w, &s.windows, link) n++;
    return n;
}

static int client_count(void)
{
    int n = 0;
    struct wl_list *clients = wl_display_get_client_list(s.display);
    for (struct wl_list *it = clients->next; it != clients; it = it->next) n++;
    return n;
}

/* Signal every client process still connected. */
static int signal_clients(int sig)
{
    int n = 0;
    struct wl_list *clients = wl_display_get_client_list(s.display);
    for (struct wl_list *it = clients->next; it != clients; it = it->next) {
        struct wl_client *c = wl_client_from_link(it);
        pid_t pid = 0;
        wl_client_get_credentials(c, &pid, NULL, NULL);
        if (pid > 0 && pid != getpid()) {
            kill(pid, sig);
            n++;
        }
    }
    return n;
}

/* The end, step one: ask every Linux window to close and keep going --
 * Family mruby's picture still on screen, so a program can ask about
 * unsaved work and be answered -- until they have gone or LINGER_MS. */
static void linger(void)
{
    int asked = 0;
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (w->toplevel->base->initialized) {
            wlr_xdg_toplevel_send_close(w->toplevel);
            asked++;
        }
    }
    if (asked == 0) return;
    fprintf(stderr, "native: compositor: asked %d window%s to close\n", asked, asked == 1 ? "" : "s");
    uint16_t *frame = malloc((size_t)640 * 360 * sizeof(uint16_t));
    uint64_t t0 = now_ms();
    uint64_t closed_at = 0;
    uint32_t shown = 0;
    while (now_ms() - t0 < LINGER_MS && s.output != NULL) {
        /* Once the windows have gone, a moment more for their programs to
         * leave on their own. */
        if (window_count() == 0) {
            if (closed_at == 0) closed_at = now_ms();
            if (client_count() == 0 || now_ms() - closed_at >= CLIENT_LEAVE_MS) break;
        }
        wl_display_flush_clients(s.display);
        wl_event_loop_dispatch(s.loop, 5);
        take_from_fmrb();
        wl_display_flush_clients(s.display);
        reap_child();
        if (frame != NULL && native_frame_seq() != shown) {
            int fw = 0, fh = 0;
            if (native_frame_copy(frame, 640 * 360, &fw, &fh, &shown)) wlr_present(frame, fw, fh, true);
        } else if (frame != NULL && s.recompose && s.route.fb_w > 0) {
            wlr_present(frame, s.route.fb_w, s.route.fb_h, false);
        }
    }
    int left = window_count();
    fprintf(stderr, "native: compositor: %s after %llu ms (%d client%s still connected)\n",
            left == 0 ? "every window closed" : "windows still open", (unsigned long long)(now_ms() - t0),
            client_count(), client_count() == 1 ? "" : "s");
    /* The picture keeps pointing at this copy until the scene goes; it is
     * not freed (the program ends right after). */
    s.frame = NULL;
}

/* Step two: the clients still connected are ended. */
static void end_clients(void)
{
    if (client_count() == 0) return;
    int n = signal_clients(SIGTERM);
    fprintf(stderr, "native: compositor: SIGTERM to %d client%s still connected\n", n, n == 1 ? "" : "s");
    uint64_t t0 = now_ms();
    while (client_count() > 0 && now_ms() - t0 < TERM_GRACE_MS) {
        wl_event_loop_dispatch(s.loop, 5);
        wl_display_flush_clients(s.display);
    }
    if (client_count() > 0) {
        n = signal_clients(SIGKILL);
        fprintf(stderr, "native: compositor: SIGKILL to %d client%s\n", n, n == 1 ? "" : "s");
        t0 = now_ms();
        while (client_count() > 0 && now_ms() - t0 < TERM_GRACE_MS) {
            wl_event_loop_dispatch(s.loop, 5);
        }
    }
}

static void wlr_close(void)
{
    if (s.display == NULL) return;
    linger();
    end_clients();
    comp_link_detach();
    native_frame_set_wake_fd(-1);
    stop_child();
    wl_display_destroy_clients(s.display);
    if (s.wake_source != NULL) wl_event_source_remove(s.wake_source);
    if (s.wake_fd >= 0) close(s.wake_fd);
    /* Not torn down further: the program _exits right after, and the
     * machine's threads may still be mid-frame. */
    s.display = NULL;
}

const native_output_t native_output_wlroots = {
    .name    = "wlroots",
    .open    = wlr_open,
    .pump    = wlr_pump,
    .present = wlr_present,
    .close   = wlr_close,
    .ctl     = wlr_ctl,
};
