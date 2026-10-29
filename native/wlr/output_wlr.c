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
 * holes, comp_route.h); the popups of the Linux windows; the IME's; the
 * screen lock (L13), over everything.
 * A Family mruby window or menu over a stand-in covers the Linux window
 * there, so the two kinds of windows stack freely.
 *
 * The screen (L9d): with screen.form "output" the frame is the output over
 * display.scale (main_native.c asks wlr_size once the output is up), so it
 * fills the output at that scale. A fullscreen app smaller than the frame
 * (Modern's 426x240) is shown alone: Family mruby's side names its
 * rectangle (the view, comp_link.h), and only that part of the picture is
 * laid on the output at its own largest whole scale (the scene buffer's
 * source box), the Linux windows hidden as under any fullscreen app.
 * display.mode sets the output's mode (a custom one on the headless and
 * nested backends, for the tests and a window of a chosen size).
 *
 * Pointer: over a hole (or a popup) it goes to that Linux window at the
 * output's own resolution, with wlroots' cursor (Family mruby's is hidden);
 * a press there also goes to Family mruby, whose window manager raises and
 * focuses the stand-in as for any click. Anywhere else it goes to Family
 * mruby. Keys go where Family mruby's focus is (a stand-in's: its Linux
 * window), except Ctrl+Tab / Ctrl+comma, which are always Family mruby's
 * (switching windows). The windows' keymap follows keyboard_layout.
 *
 * Dialogs (L9c): a window with a parent (xdg set_parent, X11
 * WM_TRANSIENT_FOR) is a Linux window like the others, with its stand-in;
 * the notice of it names the parent, and Family mruby's side puts it in the
 * middle of the parent's and keeps it above (wl_bridge.c). Menus and
 * tooltips (xdg popups, X11 override-redirect windows) have no stand-in and
 * lie over the picture, but never outside its rectangle: an xdg popup is
 * unconstrained against it (its positioner's slide, flip and resize), cut
 * at its edge if it still reaches out; an X11 menu is moved into it. A
 * button pressed anywhere but on a window's popups closes them (popup_done),
 * as a click outside a menu does on any desktop.
 *
 * Linux windows are told to draw no decoration of their own
 * (xdg-decoration, server side): the stand-in draws the frame. Move and
 * resize requests become moves and resizes of the stand-in.
 *
 * The PC's windows and screen (L13):
 *   maximize    a window's request (xdg, X11 _NET_WM_STATE) is passed to
 *               Family mruby's side, whose window operations
 *               (native_winops.h: also a title's double-click and
 *               Super+arrows, which this file takes from the keyboard
 *               whoever has it) lay the stand-in out; each window is told
 *               how it is laid out (maximized, tiled left / right).
 *               Fullscreen requests are answered without a change.
 *   lock        ext-session-lock: see "the screen lock" below. The lock
 *               program (compositor.locker) is started by the power menu's
 *               Lock and by logind's Lock and PrepareForSleep
 *               (native/sys/linux_logind.c).
 *
 * The clipboard (L12a, wl_clip.c): the machine's clipboard is the seat's
 * selection, both ways -- what a Family mruby app copies becomes the
 * selection (the compositor its owner), and a selection a Linux program sets
 * (X11 ones too, through Xwayland's bridge) is read and handed to Family
 * mruby, never waiting. The primary selection is not served.
 *
 * Also served: wlr-screencopy and xdg-output, so grim takes the composited
 * screen; ext-image-copy-capture with the output capture source, so the
 * desktop portal's wlroots backend shares the screen (L14, a browser's
 * meeting, a recorder), the portal told where the screen is
 * (activation_env); and requests on the control socket ("comp ...", see wlr_ctl
 * below) that drive a keyboard and a pointer of the tools' own through the
 * same handlers as real devices, and report sizes and timings.
 *
 * X11 programs (L9b): Xwayland, started by wlroots when the first X11
 * program connects (lazy), never at start. Its windows are Linux windows
 * like the xdg ones: the same numbering in comp_route, a stand-in each,
 * placed with wlr_xwayland_surface_configure (at their place on the output,
 * so their menus land where the program means them). Every X11 window gets
 * the stand-in's frame, whatever _MOTIF_WM_HINTS says: one that draws its
 * own title bar shows two (logged with its decorations), since without the
 * frame it could not be moved or closed the Family mruby way. Override-
 * redirect windows (X11 menus, tooltips) go into the popup layer, over the
 * picture, at their own place. No Xwayland installed: one line says X11
 * programs cannot run, DISPLAY is unset, the rest goes on.
 *
 * A window that settles on a size of its own (foot to its cell grid, an X11
 * program asking for one) is told to Family mruby (COMP_NOTICE_FIT), whose
 * stand-in then fits it; what the scale still leaves between the two (under
 * one frame pixel) shows the stand-in's frame colour (the backdrop under
 * each window), not black.
 *
 * compositor.autostart, when set, is started once through /bin/sh -c in a
 * session of its own with WAYLAND_DISPLAY naming this compositor (also set
 * in the program's environment, so the Shell's "!" programs inherit it, and
 * DISPLAY naming Xwayland's). The launcher's Linux applications
 * (native/sys/linux_apps.h, L9b) are started from this thread too, and give
 * their windows' stand-ins the entries' names.
 *
 * On a console (the login screen's session) the backend takes the seat
 * through logind or seatd (libseat); Ctrl+Alt+F1..F12 switch virtual
 * terminals. display.rotate turns the output (its transform), the pointer
 * with it.
 *
 * The end (close): every Linux window is asked to close, and the loop goes
 * on -- Family mruby still on screen -- until they have all gone or
 * LINGER_MS has passed, so a program can ask about unsaved work. Then the
 * clients still connected (and the X11 programs) get SIGTERM, and SIGKILL
 * half a second later; then whatever the launcher started that is still
 * there, windowless or not; the autostart's session last.
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
#include <sys/prctl.h>
#include <sys/wait.h>
#include <spawn.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/config.h>
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
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/version.h>
#if WLR_HAS_XWAYLAND
#include <wlr/xwayland.h>
#include <wlr/xwayland/server.h>
#endif
#include <xkbcommon/xkbcommon.h>

#include "comp_link.h"
#include "comp_route.h"
#include "fmrb_hal_file.h"
#include "fmrb_keymap.h"
#include "linux_apps.h"
#include "linux_logind.h"
#include "linux_keys.h"
#include "native_backends.h"
#include "native_ctl.h"
#include "native_clip.h"
#include "native_frame.h"
#include "native_input.h"
#include "native_winops.h"
#include "wl_clip.h"
#include "wl_ime.h"

/* Pointer motion to Family mruby is passed on at most this often (the
 * machine redraws its cursor per event), as from the SDL window. */
#define MOTION_INTERVAL_MS 16
/* How long the end waits for the Linux windows to close when asked. */
#define LINGER_MS 5000
/* After the last window, how long its program has to disconnect by itself. */
#define CLIENT_LEAVE_MS 1000
/* Then SIGTERM, and SIGKILL this much later. */
#define TERM_GRACE_MS 500
/* An X11 program's own size stands this long for its stand-in to fit it. */
#define FIT_HOLD_MS 500
/* Where the launcher's Linux icons go (a Family mruby path; L9b), and the
 * desktop name the .desktop entries are matched against (the session's
 * DesktopNames, native/package/family-mruby.desktop). */
#define LINUX_ICON_DIR "/var/cache/linux"
#define DESKTOP_NAME "family-mruby"
/* Where a key went down: Family mruby, a window (> 0), or the IME (L12b). */
#define KEY_TO_IME (-1)

/* ------------------------------------------------------------- state */

typedef enum { WIN_XDG = 0, WIN_X11 } win_kind_t;

typedef struct window {
    struct wl_list link;
    int id;                                 /* > 0 while it has a number (X11: while mapped) */
    win_kind_t kind;
    struct wlr_xdg_toplevel *toplevel;      /* WIN_XDG */
    struct wlr_scene_tree *tree;            /* xdg: its tree, origin = geometry's top-left; X11: ours */
    struct wlr_scene_tree *content;         /* X11: the surface's tree in it, while mapped */
    struct wlr_scene_tree *popups;          /* its popups, in the popup layer */
    struct wlr_scene_rect *backdrop;        /* under it, its frame's colour where it does not reach */
    struct wlr_xdg_toplevel_decoration_v1 *deco;
    bool mapped;
    int asked_x, asked_y;                   /* X11: the last place configured */
    int asked_w, asked_h;                   /* the last size asked (configure) */
    uint32_t asked_serial;                  /* xdg: that configure's serial */
    int fit_w, fit_h;                       /* the last size of its own told to Family mruby */
    int hold_w, hold_h;                     /* X11: the size its stand-in will ask once fitted */
    struct wlr_box clip;                    /* the cut last set on its content (surface coordinates) */
    uint64_t hold_until_ms;                 /* X11: its own size stands until then (0: none) */
    struct wl_listener map, unmap, commit, destroy;
    struct wl_listener set_title, request_move, request_resize, request_maximize, request_fullscreen;
    struct wl_listener set_parent;
    int told_state;                         /* COMP_STATE_* last told to it (L13) */
#if WLR_HAS_XWAYLAND
    struct wlr_xwayland_surface *xs;        /* WIN_X11 */
    bool or_win;                            /* override-redirect: a menu or tooltip, no stand-in */
    struct wl_listener associate, dissociate, request_configure, set_geometry;
    struct wl_listener x_request_maximize;
#endif
} window_t;

/* A lock surface (ext-session-lock, L13): one per output. */
typedef struct lock_surface {
    struct wl_list link;
    struct wlr_session_lock_surface_v1 *wlr;
    struct wlr_scene_tree *tree;
    struct wl_listener map, destroy;
} lock_surface_t;

typedef struct popup {
    struct wl_list link;                    /* s.popups, parents before their children */
    struct wlr_xdg_popup *popup;
    struct wlr_scene_tree *tree;            /* its scene (in its window's popups tree) */
    int win_id;                             /* the window it belongs to (its root toplevel's) */
    struct wl_listener commit, reposition, destroy;
} popup_t;

typedef struct deco {
    struct wlr_xdg_toplevel_decoration_v1 *wlr;
    struct wl_listener request_mode, destroy;
} deco_t;

typedef struct keyboard {
    struct wl_list link;
    struct wlr_keyboard *wlr;
    bool own_keymap;                        /* the IME's virtual keyboard: its keymap is its own */
    int key_target[KEY_MAX + 1];            /* where each held key went down */
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
    struct wlr_scene_tree *ime_tree;        /* the IME's candidates and composing text (L12b) */
    struct wlr_scene_tree *lock_tree;       /* the screen lock, over everything (L13) */
    bool ime;                               /* input.ime = "wayland" */
    struct wlr_xdg_shell *xdg_shell;
    struct wlr_xdg_decoration_manager_v1 *deco_mgr;
    struct wlr_compositor *compositor;
    struct wlr_session *session;            /* on a console: the seat (logind / seatd) */
    int rotate;                             /* display.rotate: the output's transform */
    int mode_w, mode_h, mode_hz;            /* display.mode, 0x0 = the output's preferred */
#if WLR_HAS_XWAYLAND
    struct wlr_xwayland *xwayland;          /* NULL: none (not installed, or failed) */
    bool xwayland_up;                       /* the server has started (lazily) */
    struct wl_listener xw_new_surface, xw_ready;
#endif
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *xcursor;
    struct wlr_seat *seat;
    struct wl_list windows;                 /* window_t */
    struct wl_list popups;                  /* popup_t */
    struct wl_list keyboards;               /* keyboard_t */
    bool super_held[2];                     /* left and right Super (L13) */

    /* The screen lock (L13). */
    struct {
        struct wlr_session_lock_manager_v1 *mgr;
        struct wlr_session_lock_v1 *lock;   /* the lock program's, NULL none (or it died) */
        bool locked;                        /* covered: no input to Family mruby or a window */
        bool sent_locked;
        struct wlr_scene_rect *cover;       /* what shows while locked, under the lock surfaces */
        struct wl_list surfaces;            /* lock_surface_t */
        struct wlr_surface *focus;          /* the lock surface with the keyboard */
        char program[NATIVE_MACHINE_STR];   /* compositor.locker */
        bool available;                     /* the program is installed */
        pid_t pid;                          /* the program started, 0 none */
        bool awaiting;                      /* started, its lock not asked for yet */
        uint64_t started_ms;
        unsigned locks, unlocks, died, keys_held_back, starts;
        struct wl_listener new_lock, lock_unlock, lock_destroy, lock_new_surface;
    } lk;
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
        unsigned view_changes;              /* the shown part changed with a frame (L9e) */
        unsigned pointer_rechecks;          /* what a still pointer is over changed (L9e) */
        unsigned x11_menu_presses;          /* presses passed to an X11 menu (L9e) */
        unsigned to_fmrb;                   /* input events pushed to Family mruby (L13) */
        unsigned winops;                    /* Super+arrows taken (L13) */
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
    if (id <= 0) return NULL;
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (w->id == id) return w;
    }
    return NULL;
}

/* The id of the window an xdg toplevel is, 0 when it has none (not mapped). */
static int toplevel_id(const struct wlr_xdg_toplevel *t)
{
    if (t == NULL) return 0;
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (w->kind == WIN_XDG && w->toplevel == t) return w->mapped ? w->id : 0;
    }
    return 0;
}

/* ---------------------------------------------- xdg and X11 windows alike */

static bool is_or(const window_t *w)
{
#if WLR_HAS_XWAYLAND
    return w->kind == WIN_X11 && w->or_win;
#else
    (void)w;
    return false;
#endif
}

static struct wlr_surface *win_surface(const window_t *w)
{
#if WLR_HAS_XWAYLAND
    if (w->kind == WIN_X11) return w->xs != NULL ? w->xs->surface : NULL;
#endif
    return w->toplevel->base->surface;
}

/* Where the window's own content starts in its surface (xdg: its geometry,
 * which may leave room for a shadow; X11: the surface's corner). */
static struct wlr_box win_geometry(const window_t *w)
{
#if WLR_HAS_XWAYLAND
    if (w->kind == WIN_X11) return (struct wlr_box){ 0, 0, w->xs->width, w->xs->height };
#endif
    return w->toplevel->base->geometry;
}

static void win_activate(window_t *w, bool on)
{
#if WLR_HAS_XWAYLAND
    if (w->kind == WIN_X11) {
        wlr_xwayland_surface_activate(w->xs, on);
        if (on) wlr_xwayland_surface_restack(w->xs, NULL, XCB_STACK_MODE_ABOVE);
        return;
    }
#endif
    wlr_xdg_toplevel_set_activated(w->toplevel, on);
}

/* Ask it to close (an xdg close; WM_DELETE_WINDOW, or the X11 kill when the
 * program does not take that). */
static void win_close(window_t *w)
{
#if WLR_HAS_XWAYLAND
    if (w->kind == WIN_X11) {
        wlr_xwayland_surface_close(w->xs);
        return;
    }
#endif
    wlr_xdg_toplevel_send_close(w->toplevel);
}

/* The process behind it, 0 when unknown. */
static pid_t win_pid(const window_t *w)
{
#if WLR_HAS_XWAYLAND
    if (w->kind == WIN_X11) return w->xs->pid;
#endif
    pid_t pid = 0;
    wl_client_get_credentials(wl_resource_get_client(w->toplevel->resource), &pid, NULL, NULL);
    return pid;
}

/* The name its stand-in carries (the taskbar's): the launcher entry's when
 * one matches (linux_apps.h), else app_id. */
static void window_name(const char *app_id, const char *alt, pid_t pid, char *out, size_t cap)
{
    if (linux_apps_available() && linux_apps_window_name(app_id, alt, pid, out, cap)) return;
    snprintf(out, cap, "%s", app_id && app_id[0] ? app_id : (alt && alt[0] ? alt : "linux"));
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

/* The backdrop takes the stand-in's frame colour: its left border, halfway
 * down (frame pixels, RGB565), black before there is a frame. */
static void set_backdrop_colour(window_t *w, const comp_window_t *cw)
{
    float c[4] = { 0, 0, 0, 1 };
    const comp_route_t *r = &s.route;
    int fx = cw->fx - 1, fy = cw->fy + cw->fh / 2;
    if (s.frame != NULL && fx >= 0 && fy >= 0 && fx < r->fb_w && fy < r->fb_h) {
        uint16_t p = s.frame[fy * r->fb_w + fx];
        c[0] = (float)((p >> 11) & 0x1F) / 31.0f;
        c[1] = (float)((p >> 5) & 0x3F) / 63.0f;
        c[2] = (float)(p & 0x1F) / 31.0f;
    }
    wlr_scene_rect_set_color(w->backdrop, c);
}

/* One window's scene from the route: shown or not, where, clipped to its
 * content rectangle, and asked to take that size. */
/* Tell the window how Family mruby has laid it out (L13): maximized, a half
 * (xdg's tiled edges). Only on a change. */
static void tell_state(window_t *w, const comp_window_t *cw)
{
    if (!w->mapped) return;
    int st = cw->state;
    if (st == w->told_state) return;
    const char *what = st == COMP_STATE_MAX ? "maximized" : st == COMP_STATE_LEFT ? "the left half"
                     : st == COMP_STATE_RIGHT ? "the right half" : "as placed";
#if WLR_HAS_XWAYLAND
    if (w->kind == WIN_X11) {
        w->told_state = st;
        wlr_xwayland_surface_set_maximized(w->xs, st == COMP_STATE_MAX, st == COMP_STATE_MAX);
        fprintf(stderr, "native: compositor: window %d (X11) told: %s\n", w->id, what);
        return;
    }
#endif
    if (!w->toplevel->base->initialized) return;
    w->told_state = st;
    wlr_xdg_toplevel_set_maximized(w->toplevel, st == COMP_STATE_MAX);
    if (wl_resource_get_version(w->toplevel->resource) >= XDG_TOPLEVEL_STATE_TILED_LEFT_SINCE_VERSION) {
        uint32_t edges = st == COMP_STATE_MAX ? (WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT)
                       : st == COMP_STATE_LEFT ? (WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT)
                       : st == COMP_STATE_RIGHT ? (WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT)
                       : WLR_EDGE_NONE;
        wlr_xdg_toplevel_set_tiled(w->toplevel, edges);
    }
    fprintf(stderr, "native: compositor: window %d told: %s\n", w->id, what);
}

/* Cut the window's content to the stand-in's content (L15): set when the
 * box differs from the last set, so it is cheap to ask on every commit. A
 * window drawing beyond the size asked (a browser restoring its session, a
 * page in a subsurface) never shows outside its stand-in, also not through
 * another window's hole. */
static void set_clip(window_t *w, struct wlr_scene_node *node, struct wlr_box box)
{
    if (box.x == w->clip.x && box.y == w->clip.y && box.width == w->clip.width && box.height == w->clip.height) {
        return;
    }
    w->clip = box;
    wlr_scene_subsurface_tree_set_clip(node, &box);
}

static void apply_window(window_t *w)
{
    const comp_window_t *cw = comp_route_window(&s.route, w->id);
    bool on = w->mapped && cw != NULL && cw->visible;
    wlr_scene_node_set_enabled(&w->tree->node, on);
    wlr_scene_node_set_enabled(&w->popups->node, on);
    wlr_scene_node_set_enabled(&w->backdrop->node, on);
    if (cw == NULL || !cw->placed) return;
    tell_state(w, cw);
    /* The scene's xdg tree puts the window's geometry (not its surface,
     * which may reach out for a shadow) at the tree's origin; ours for an
     * X11 window holds its surface at the origin. */
    wlr_scene_node_set_position(&w->tree->node, cw->x, cw->y);
    wlr_scene_node_set_position(&w->popups->node, cw->x, cw->y);
    int ww = 0, wh = 0;
    if (!comp_route_wanted_size(&s.route, w->id, &ww, &wh)) return;
    wlr_scene_node_set_position(&w->backdrop->node, cw->x, cw->y);
    wlr_scene_rect_set_size(w->backdrop, ww, wh);
    set_backdrop_colour(w, cw);
    wlr_scene_node_place_below(&w->backdrop->node, &w->tree->node);
#if WLR_HAS_XWAYLAND
    if (w->kind == WIN_X11) {
        if (w->content != NULL) set_clip(w, &w->content->node, (struct wlr_box){ 0, 0, ww, wh });
        if (!w->mapped || ww <= 0 || wh <= 0) return;
        /* After the program asked for a size of its own, it keeps it until
         * the stand-in has fitted it (or FIT_HOLD_MS). */
        if (w->hold_until_ms != 0) {
            if ((ww != w->hold_w || wh != w->hold_h) && now_ms() < w->hold_until_ms) {
                ww = w->asked_w;
                wh = w->asked_h;
            } else {
                w->hold_until_ms = 0;
            }
        }
        if (ww != w->asked_w || wh != w->asked_h || cw->x != w->asked_x || cw->y != w->asked_y) {
            w->asked_x = cw->x;
            w->asked_y = cw->y;
            w->asked_w = ww;
            w->asked_h = wh;
            wlr_xwayland_surface_configure(w->xs, (int16_t)cw->x, (int16_t)cw->y, (uint16_t)ww, (uint16_t)wh);
        }
        return;
    }
#endif
    struct wlr_box g = w->toplevel->base->geometry;
    set_clip(w, &w->tree->node, (struct wlr_box){ g.x, g.y, ww, wh });
    if (w->mapped && (ww != w->asked_w || wh != w->asked_h) && ww > 0 && wh > 0) {
        w->asked_w = ww;
        w->asked_h = wh;
        w->asked_serial = wlr_xdg_toplevel_set_size(w->toplevel, ww, wh);
    }
}

/* A popup cut at the picture's edge, whatever its positioner allowed: the
 * clip is in its own surface's coordinates (its scene tree's place on the
 * output less its geometry's offset). Its children get theirs after it (the
 * list has parents first), since a clip reaches the subsurface trees under
 * the node. */
static void popup_clip(popup_t *p)
{
    int tx = 0, ty = 0;
    wlr_scene_node_coords(&p->tree->node, &tx, &ty);
    struct wlr_box g = p->popup->base->current.geometry;
    int bx = 0, by = 0, bw = 0, bh = 0;
    comp_route_picture_box(&s.route, tx - g.x, ty - g.y, &bx, &by, &bw, &bh);
    struct wlr_box clip = { .x = bx, .y = by, .width = bw, .height = bh };
    wlr_scene_subsurface_tree_set_clip(&p->tree->node, &clip);
}

static void clip_popups(void)
{
    popup_t *p;
    wl_list_for_each(p, &s.popups, link) popup_clip(p);
}

/* Close every popup but those of window keep (a press elsewhere). */
static void close_popups(int keep)
{
    int n = 0;
    for (int guard = 0, again = 1; again && guard < 64; guard++) {
        again = 0;
        popup_t *p;
        wl_list_for_each(p, &s.popups, link) {
            if (p->win_id == keep) continue;
            /* The outermost of each chain: wlroots closes its children with
             * it, which takes them off the list -- so the walk starts over. */
            struct wlr_xdg_surface *ps = wlr_xdg_surface_try_from_wlr_surface(p->popup->parent);
            if (ps != NULL && ps->role == WLR_XDG_SURFACE_ROLE_POPUP) continue;
            wlr_xdg_popup_destroy(p->popup);
            n++;
            again = 1;
            break;
        }
    }
    if (n > 0) fprintf(stderr, "native: compositor: %d menu%s closed (a press elsewhere)\n", n, n == 1 ? "" : "s");
}

#if WLR_HAS_XWAYLAND
/* An X11 menu or tooltip moved into the picture's rectangle (the least move)
 * and cut at its edge if larger. */
static void or_keep_inside(window_t *w)
{
    struct wlr_xwayland_surface *xs = w->xs;
    int x = xs->x, y = xs->y;
    if (comp_route_inside(&s.route, &x, &y, xs->width, xs->height)) {
        fprintf(stderr, "native: compositor: X11 menu %d %dx%d moved from (%d,%d) to (%d,%d), inside the picture\n",
                w->id, xs->width, xs->height, xs->x, xs->y, x, y);
        wlr_xwayland_surface_configure(xs, (int16_t)x, (int16_t)y, xs->width, xs->height);
    }
    wlr_scene_node_set_position(&w->tree->node, x, y);
    int bx = 0, by = 0, bw = 0, bh = 0;
    comp_route_picture_box(&s.route, x, y, &bx, &by, &bw, &bh);
    struct wlr_box clip = { .x = bx, .y = by, .width = bw, .height = bh };
    wlr_scene_subsurface_tree_set_clip(&w->tree->node, &clip);
}
#endif

/* Place the black, the picture and the windows from the route. */
static void apply_layout(void)
{
    const comp_route_t *r = &s.route;
    wlr_scene_rect_set_size(s.black, r->out_w, r->out_h);
    /* What is shown: the whole frame, or the part Family mruby asked for
     * (a fullscreen app smaller than the screen, L9d), each at its own
     * largest whole scale. */
    wlr_scene_node_set_position(&s.picture->node, r->show.x, r->show.y);
    wlr_scene_buffer_set_dest_size(s.picture, r->show.w, r->show.h);
    if (r->viewing) {
        struct wlr_fbox src = { .x = r->view_x, .y = r->view_y, .width = r->view_w, .height = r->view_h };
        wlr_scene_buffer_set_source_box(s.picture, &src);
    } else {
        wlr_scene_buffer_set_source_box(s.picture, NULL);
    }
    /* Bottom to top, as the route has them. */
    for (int i = 0; i < r->n; i++) {
        window_t *w = window_by_id(r->win[i].id);
        if (w == NULL) continue;
        wlr_scene_node_raise_to_top(&w->tree->node);
        wlr_scene_node_raise_to_top(&w->popups->node);
        apply_window(w);
    }
    /* X11 menus and tooltips over every window's popups. */
    window_t *w;
    wl_list_for_each_reverse(w, &s.windows, link) {
        if (is_or(w) && w->mapped) wlr_scene_node_raise_to_top(&w->tree->node);
    }
    clip_popups();
    wl_ime_relayout();
    s.recompose = true;
    static fmrb_present_place_t logged;
    if (memcmp(&logged, &r->show, sizeof(logged)) != 0) {
        logged = r->show;
        if (r->viewing) {
            fprintf(stderr, "native: compositor: output %dx%d: %dx%d of the frame %dx%d (a fullscreen app) at %dx, "
                            "%dx%d at (%d,%d), black around\n",
                    r->out_w, r->out_h, r->view_w, r->view_h, r->fb_w, r->fb_h, r->show.scale, r->show.w,
                    r->show.h, r->show.x, r->show.y);
        } else {
            fprintf(stderr, "native: compositor: output %dx%d: frame %dx%d at %dx, %dx%d at (%d,%d), black around\n",
                    r->out_w, r->out_h, r->fb_w, r->fb_h, r->place.scale, r->place.w, r->place.h,
                    r->place.x, r->place.y);
        }
    }
    int bw = 0, bh = 0;
    native_frame_base_size(&bw, &bh);
    comp_link_set_geometry(r->place.scale, bw, bh);
}

static void lock_relayout(void);

static void relayout(void)
{
    int ow = 0, oh = 0;
    if (s.output != NULL) wlr_output_effective_resolution(s.output, &ow, &oh);
    if (comp_route_layout(&s.route, ow, oh, s.route.fb_w, s.route.fb_h)) {
        apply_layout();
        lock_relayout();
    }
}

/* ---------------------------------------------------------- the focus */

/* Where a Linux window's surface has its origin on the output (the IME's
 * candidates go by its text cursor, L12b). */
static bool surface_origin(struct wlr_surface *surface, int *x, int *y)
{
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (!w->mapped || w->id <= 0 || win_surface(w) != surface) continue;
        int tx = 0, ty = 0;
        if (!wlr_scene_node_coords(&w->tree->node, &tx, &ty)) return false;
        if (w->kind == WIN_XDG) {
            struct wlr_box g = win_geometry(w);
            tx -= g.x;
            ty -= g.y;
        }
        *x = tx;
        *y = ty;
        return true;
    }
    return false;
}

static void apply_focus(void)
{
    /* Locked (L13): the lock surface keeps the keyboard. */
    if (s.lk.locked) return;
    int focus = comp_route_focus(&s.route);
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (w->mapped && w->id > 0 && !is_or(w) && w->id != focus) win_activate(w, false);
    }
    window_t *fw = focus != COMP_FMRB ? window_by_id(focus) : NULL;
    struct wlr_surface *surf = (fw != NULL && fw->mapped) ? win_surface(fw) : NULL;
    if (surf == NULL) {
        wlr_seat_keyboard_notify_clear_focus(s.seat);
        wl_ime_focus(NULL, focus == COMP_FMRB);
        return;
    }
    win_activate(fw, true);
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(s.seat);
    if (kb != NULL) {
        wlr_seat_keyboard_notify_enter(s.seat, surf, kb->keycodes, kb->num_keycodes, &kb->modifiers);
    } else {
        wlr_seat_keyboard_notify_enter(s.seat, surf, NULL, 0, NULL);
    }
    wl_ime_focus(surf, false);
}

static void recheck_pointer(void);
static void lock_start(const char *why);

/* Family mruby's places, stacking and focus, and its requests. */
static void take_from_fmrb(void)
{
    comp_places_t pl;
    if (comp_link_take_places(&pl)) {
        int ch = comp_route_place(&s.route, pl.p, pl.n, pl.focus);
        if (ch & (COMP_PLACE_MOVED | COMP_PLACE_ORDER)) {
            apply_layout();
            recheck_pointer();
        }
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
            win_close(w);
        } else if (kind == COMP_CMD_LOCK) {
            lock_start(id == 1 ? "logind asked" : "the power menu's Lock");
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
    char name[COMP_TEXT];
    window_name(app_id, NULL, win_pid(w), name, 32);
    int parent = toplevel_id(w->toplevel->parent);
    comp_route_window_add(&s.route, w->id, g.width, g.height, parent, name);
    if (w->toplevel->title != NULL) {
        comp_route_notice(&s.route, COMP_NOTICE_TITLE, w->id, 0, 0, 0, 0, w->toplevel->title);
    }
    w->told_state = COMP_STATE_NONE;
    /* Asked before it was mapped (a program that starts maximized). */
    if (w->toplevel->requested.maximized) comp_route_notice(&s.route, COMP_NOTICE_MAXIMIZE, w->id, 1, 0, 0, 0, NULL);
    char rel[32] = "";
    if (parent > 0) snprintf(rel, sizeof(rel), ", a dialog of window %d", parent);
    fprintf(stderr, "native: compositor: window %d \"%s\" %dx%d (decoration: %s; app id %s, named %s%s)\n", w->id,
            w->toplevel->title ? w->toplevel->title : "", g.width, g.height,
            w->deco != NULL ? "server side" : "none asked", app_id, name, rel);
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
        /* Maximize is offered (L13): Family mruby's window operations do
         * it. Fullscreen is not. */
        if (wl_resource_get_version(w->toplevel->resource) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
            wlr_xdg_toplevel_set_wm_capabilities(w->toplevel, WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE);
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
    } else if (cw != NULL && cw->placed) {
        /* Every commit: the cut follows the geometry's offset (a toolkit's
         * shadow comes and goes) (L15). */
        int ww = 0, wh = 0;
        if (comp_route_wanted_size(&s.route, w->id, &ww, &wh)) {
            set_clip(w, &w->tree->node, (struct wlr_box){ g.x, g.y, ww, wh });
        }
    }
    /* Its answer to the size last asked is a size of its own (foot keeps to
     * its cell grid): the stand-in is to fit it, once per size. */
    cw = comp_route_window(&s.route, w->id);
    if (cw != NULL && cw->placed && w->asked_w > 0 && w->toplevel->base->current.configure_serial == w->asked_serial &&
        (g.width != w->asked_w || g.height != w->asked_h) && (g.width != w->fit_w || g.height != w->fit_h)) {
        w->fit_w = g.width;
        w->fit_h = g.height;
        comp_route_notice(&s.route, COMP_NOTICE_FIT, w->id, 0, 0, g.width, g.height, NULL);
        fprintf(stderr, "native: compositor: window %d took %dx%d for the %dx%d asked; its stand-in fits it\n",
                w->id, g.width, g.height, w->asked_w, w->asked_h);
    }
    if (s.stats.key_sent_ms != 0 && w->id == comp_route_focus(&s.route)) {
        uint64_t lat = now_ms() - s.stats.key_sent_ms;
        s.stats.key_sent_ms = 0;
        s.stats.key_lat_n++;
        s.stats.key_lat_sum_ms += lat;
        if (lat > s.stats.key_lat_max_ms) s.stats.key_lat_max_ms = lat;
    }
}

/* A parent set (or unset) after the window appeared. */
static void window_set_parent(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, set_parent);
    if (!w->mapped) return;
    int parent = toplevel_id(w->toplevel->parent);
    comp_route_notice(&s.route, COMP_NOTICE_PARENT, w->id, parent, 0, 0, 0, NULL);
    fprintf(stderr, "native: compositor: window %d: parent now %d\n", w->id, parent);
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

/* Maximize asked by the window (L13): passed to Family mruby's side, which
 * lays its stand-in out; the window is told the state with the places that
 * follow (tell_state). The protocol wants a configure in answer at once, so
 * one is sent with nothing changed yet. Before the window is mapped the
 * request waits (its requested state is read at the map). Fullscreen is not
 * offered: answered without a change. */
static void window_request_state(struct wlr_xdg_toplevel *t)
{
    if (t->base->initialized) wlr_xdg_surface_schedule_configure(t->base);
}

static void window_request_maximize(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, request_maximize);
    if (w->mapped) {
        bool on = w->toplevel->requested.maximized;
        comp_route_notice(&s.route, COMP_NOTICE_MAXIMIZE, w->id, on ? 1 : 0, 0, 0, 0, NULL);
        fprintf(stderr, "native: compositor: window %d asked to be %s\n", w->id, on ? "maximized" : "unmaximized");
    }
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
    wl_list_remove(&w->set_parent.link);
    wl_list_remove(&w->link);
    wlr_scene_node_destroy(&w->popups->node);
    wlr_scene_node_destroy(&w->backdrop->node);
    free(w);
}

static const float s_black[4] = { 0, 0, 0, 1 };

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
    w->backdrop = wlr_scene_rect_create(s.windows_tree, 0, 0, s_black);
    wlr_scene_node_set_enabled(&w->tree->node, false);
    wlr_scene_node_set_enabled(&w->popups->node, false);
    wlr_scene_node_set_enabled(&w->backdrop->node, false);
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
    w->set_parent.notify = window_set_parent;
    wl_signal_add(&t->events.set_parent, &w->set_parent);
    wl_list_insert(&s.windows, &w->link);
}

/* The popup's first configure: placed by its positioner inside the picture's
 * rectangle (in the coordinates of its window's surface, whose geometry lies
 * at the window's place). */
static void popup_unconstrain(popup_t *p)
{
    window_t *w = window_by_id(p->win_id);
    const comp_window_t *cw = w != NULL ? comp_route_window(&s.route, w->id) : NULL;
    if (cw == NULL || !cw->placed || s.route.place.w <= 0) {
        wlr_xdg_surface_schedule_configure(p->popup->base);
        return;
    }
    struct wlr_box g = w->toplevel->base->current.geometry;
    int bx = 0, by = 0, bw = 0, bh = 0;
    comp_route_picture_box(&s.route, cw->x - g.x, cw->y - g.y, &bx, &by, &bw, &bh);
    struct wlr_box box = { .x = bx, .y = by, .width = bw, .height = bh };
    wlr_xdg_popup_unconstrain_from_box(p->popup, &box);
}

static void popup_commit(struct wl_listener *l, void *data)
{
    (void)data;
    popup_t *p = wl_container_of(l, p, commit);
    if (p->popup->base->initial_commit) {
        popup_unconstrain(p);
        return;
    }
    popup_clip(p);
}

/* xdg_popup.reposition: its new positioner, inside the picture again. */
static void popup_reposition(struct wl_listener *l, void *data)
{
    (void)data;
    popup_t *p = wl_container_of(l, p, reposition);
    popup_unconstrain(p);
}

static void popup_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    popup_t *p = wl_container_of(l, p, destroy);
    wl_list_remove(&p->commit.link);
    wl_list_remove(&p->reposition.link);
    wl_list_remove(&p->destroy.link);
    wl_list_remove(&p->link);
    free(p);
}

static void new_popup(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_xdg_popup *xp = data;
    struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xp->parent);
    if (parent == NULL || parent->data == NULL) return;
    /* Its window: up the chain of popups to the toplevel. */
    struct wlr_xdg_surface *root = parent;
    while (root != NULL && root->role == WLR_XDG_SURFACE_ROLE_POPUP && root->popup != NULL) {
        root = wlr_xdg_surface_try_from_wlr_surface(root->popup->parent);
    }
    if (root == NULL || root->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) return;
    popup_t *p = calloc(1, sizeof(*p));
    if (p == NULL) return;
    p->popup = xp;
    p->win_id = toplevel_id(root->toplevel);
    p->tree = wlr_scene_xdg_surface_create(parent->data, xp->base);
    xp->base->data = p->tree;
    p->commit.notify = popup_commit;
    wl_signal_add(&xp->base->surface->events.commit, &p->commit);
    p->reposition.notify = popup_reposition;
    wl_signal_add(&xp->events.reposition, &p->reposition);
    p->destroy.notify = popup_destroy;
    wl_signal_add(&xp->events.destroy, &p->destroy);
    wl_list_insert(s.popups.prev, &p->link);
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

/* ------------------------------------------------------- X11 windows */

#if WLR_HAS_XWAYLAND
static void begin_grab(window_t *w, grab_mode_t mode, uint32_t edges);

/* The id of an X11 window's parent (WM_TRANSIENT_FOR), 0 when it has none
 * that is a Family mruby window. */
static int x11_parent_id(const struct wlr_xwayland_surface *xs)
{
    const window_t *p = xs->parent != NULL ? xs->parent->data : NULL;
    if (p == NULL || !p->mapped || p->or_win) return 0;
    return p->id;
}

static void x11_map(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, map);
    struct wlr_xwayland_surface *xs = w->xs;
    if (w->content == NULL) w->content = wlr_scene_subsurface_tree_create(w->tree, xs->surface);
    w->mapped = true;
    w->or_win = xs->override_redirect;
    w->id = ++s.next_id;
    if (w->or_win) {
        /* A menu or a tooltip: over the picture, where the program puts it
         * if that is inside the picture. */
        wlr_scene_node_reparent(&w->tree->node, s.popups_tree);
        or_keep_inside(w);
        wlr_scene_node_set_enabled(&w->tree->node, true);
        wlr_scene_node_raise_to_top(&w->tree->node);
        return;
    }
    wlr_scene_node_reparent(&w->tree->node, s.windows_tree);
    w->asked_w = w->asked_h = 0;
    w->asked_x = xs->x;
    w->asked_y = xs->y;
    w->fit_w = w->fit_h = 0;
    w->hold_until_ms = 0;
    char name[COMP_TEXT];
    window_name(xs->class, xs->instance, xs->pid, name, 32);
    int parent = x11_parent_id(xs);
    comp_route_window_add(&s.route, w->id, xs->width, xs->height, parent, name);
    if (xs->title != NULL) comp_route_notice(&s.route, COMP_NOTICE_TITLE, w->id, 0, 0, 0, 0, xs->title);
    w->told_state = COMP_STATE_NONE;
    if (xs->maximized_horz && xs->maximized_vert) {
        comp_route_notice(&s.route, COMP_NOTICE_MAXIMIZE, w->id, 1, 0, 0, 0, NULL);
    }
    char rel[32] = "";
    if (parent > 0) snprintf(rel, sizeof(rel), ", a dialog of window %d", parent);
    fprintf(stderr, "native: compositor: window %d \"%s\" %dx%d (X11 %s/%s, decorations %s; named %s%s)\n", w->id,
            xs->title ? xs->title : "", xs->width, xs->height, xs->instance ? xs->instance : "",
            xs->class ? xs->class : "",
            xs->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_ALL ? "the window manager's"
                                                                     : "its own (two title bars)",
            name, rel);
    apply_layout();
}

static void x11_unmap(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, unmap);
    w->mapped = false;
    if (w->content != NULL) {
        wlr_scene_node_destroy(&w->content->node);
        w->content = NULL;
    }
    w->clip = (struct wlr_box){ 0 };     /* the next content is cut afresh */
    int id = w->id;
    w->id = 0;
    if (w->or_win) {
        wlr_scene_node_set_enabled(&w->tree->node, false);
        if (s.pointer_target == id) s.pointer_target = COMP_FMRB;
        return;
    }
    wlr_scene_node_set_enabled(&w->tree->node, false);
    wlr_scene_node_set_enabled(&w->backdrop->node, false);
    int focus = comp_route_focus(&s.route);
    comp_route_window_remove(&s.route, id);
    fprintf(stderr, "native: compositor: window %d gone\n", id);
    if (s.grab.id == id) s.grab.mode = GRAB_NONE;
    if (s.pointer_target == id) s.pointer_target = COMP_FMRB;
    if (focus == id) apply_focus();
    apply_layout();
}

static void x11_associate(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, associate);
    w->map.notify = x11_map;
    wl_signal_add(&w->xs->surface->events.map, &w->map);
    w->unmap.notify = x11_unmap;
    wl_signal_add(&w->xs->surface->events.unmap, &w->unmap);
}

static void x11_dissociate(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, dissociate);
    wl_list_remove(&w->map.link);
    wl_list_remove(&w->unmap.link);
    wl_list_init(&w->map.link);
    wl_list_init(&w->unmap.link);
}

/* The program asks for a place and size. Before it is mapped, and for menus,
 * granted as asked. Once it is a Family mruby window its place is the stand-
 * in's, and a size of its own is granted and told to Family mruby, whose
 * stand-in then fits it (COMP_NOTICE_FIT). */
static void x11_request_configure(struct wl_listener *l, void *data)
{
    window_t *w = wl_container_of(l, w, request_configure);
    struct wlr_xwayland_surface_configure_event *ev = data;
    struct wlr_xwayland_surface *xs = w->xs;
    const comp_window_t *cw = w->mapped && !w->or_win ? comp_route_window(&s.route, w->id) : NULL;
    if (cw == NULL || !cw->placed) {
        wlr_xwayland_surface_configure(xs, ev->x, ev->y, ev->width, ev->height);
        return;
    }
    if (ev->width == w->asked_w && ev->height == w->asked_h) {
        wlr_xwayland_surface_configure(xs, (int16_t)w->asked_x, (int16_t)w->asked_y, ev->width, ev->height);
        return;
    }
    int sc = s.route.place.scale > 0 ? s.route.place.scale : 1;
    w->asked_w = ev->width;
    w->asked_h = ev->height;
    w->hold_w = (ev->width + sc - 1) / sc * sc;
    w->hold_h = (ev->height + sc - 1) / sc * sc;
    w->hold_until_ms = now_ms() + FIT_HOLD_MS;
    wlr_xwayland_surface_configure(xs, (int16_t)w->asked_x, (int16_t)w->asked_y, ev->width, ev->height);
    comp_route_window_resize(&s.route, w->id, ev->width, ev->height);
    w->fit_w = ev->width;
    w->fit_h = ev->height;
    comp_route_notice(&s.route, COMP_NOTICE_FIT, w->id, 0, 0, ev->width, ev->height, NULL);
    fprintf(stderr, "native: compositor: window %d (X11) asked to be %dx%d; its stand-in fits it\n", w->id,
            ev->width, ev->height);
}

/* A configure took effect: a menu moved, or a window's size changed. */
static void x11_set_geometry(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, set_geometry);
    struct wlr_xwayland_surface *xs = w->xs;
    if (!w->mapped) return;
    if (w->or_win) {
        or_keep_inside(w);
        return;
    }
    const comp_window_t *cw = comp_route_window(&s.route, w->id);
    if (cw != NULL && (cw->w != xs->width || cw->h != xs->height)) {
        comp_route_window_resize(&s.route, w->id, xs->width, xs->height);
    }
}

static void x11_set_title(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, set_title);
    if (!w->mapped || w->or_win || w->xs->title == NULL) return;
    comp_route_notice(&s.route, COMP_NOTICE_TITLE, w->id, 0, 0, 0, 0, w->xs->title);
}

static void x11_set_parent(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, set_parent);
    if (!w->mapped || w->or_win) return;
    int parent = x11_parent_id(w->xs);
    comp_route_notice(&s.route, COMP_NOTICE_PARENT, w->id, parent, 0, 0, 0, NULL);
    fprintf(stderr, "native: compositor: window %d (X11): parent now %d\n", w->id, parent);
}

static void x11_request_move(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, request_move);
    if (w->mapped && !w->or_win) begin_grab(w, GRAB_MOVE, 0);
}

static void x11_request_resize(struct wl_listener *l, void *data)
{
    struct wlr_xwayland_resize_event *ev = data;
    window_t *w = wl_container_of(l, w, request_resize);
    if (w->mapped && !w->or_win) begin_grab(w, GRAB_RESIZE, ev->edges);
}

/* _NET_WM_STATE's maximize asked by the program (L13): as for an xdg
 * window. The state it asks for is in the surface already; it is told the
 * one Family mruby gives with the places that follow. */
static void x11_request_maximize(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, x_request_maximize);
    if (!w->mapped || w->or_win) return;
    bool on = w->xs->maximized_horz && w->xs->maximized_vert;
    comp_route_notice(&s.route, COMP_NOTICE_MAXIMIZE, w->id, on ? 1 : 0, 0, 0, 0, NULL);
    fprintf(stderr, "native: compositor: window %d (X11) asked to be %s\n", w->id, on ? "maximized" : "unmaximized");
    w->told_state = -1;   /* tell it again, whatever Family mruby decides */
}

static void x11_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    window_t *w = wl_container_of(l, w, destroy);
    wl_list_remove(&w->map.link);
    wl_list_remove(&w->unmap.link);
    wl_list_remove(&w->destroy.link);
    wl_list_remove(&w->associate.link);
    wl_list_remove(&w->dissociate.link);
    wl_list_remove(&w->request_configure.link);
    wl_list_remove(&w->set_geometry.link);
    wl_list_remove(&w->set_title.link);
    wl_list_remove(&w->set_parent.link);
    wl_list_remove(&w->request_move.link);
    wl_list_remove(&w->request_resize.link);
    wl_list_remove(&w->x_request_maximize.link);
    wl_list_remove(&w->link);
    wlr_scene_node_destroy(&w->tree->node);
    wlr_scene_node_destroy(&w->popups->node);
    wlr_scene_node_destroy(&w->backdrop->node);
    free(w);
}

static void x11_new_surface(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_xwayland_surface *xs = data;
    window_t *w = calloc(1, sizeof(*w));
    if (w == NULL) return;
    w->kind = WIN_X11;
    w->xs = xs;
    xs->data = w;
    w->tree = wlr_scene_tree_create(s.windows_tree);
    w->tree->node.data = w;
    w->popups = wlr_scene_tree_create(s.popups_tree);
    w->backdrop = wlr_scene_rect_create(s.windows_tree, 0, 0, s_black);
    wlr_scene_node_set_enabled(&w->tree->node, false);
    wlr_scene_node_set_enabled(&w->popups->node, false);
    wlr_scene_node_set_enabled(&w->backdrop->node, false);
    wl_list_init(&w->map.link);
    wl_list_init(&w->unmap.link);
    w->destroy.notify = x11_destroy;
    wl_signal_add(&xs->events.destroy, &w->destroy);
    w->associate.notify = x11_associate;
    wl_signal_add(&xs->events.associate, &w->associate);
    w->dissociate.notify = x11_dissociate;
    wl_signal_add(&xs->events.dissociate, &w->dissociate);
    w->request_configure.notify = x11_request_configure;
    wl_signal_add(&xs->events.request_configure, &w->request_configure);
    w->set_geometry.notify = x11_set_geometry;
    wl_signal_add(&xs->events.set_geometry, &w->set_geometry);
    w->set_title.notify = x11_set_title;
    wl_signal_add(&xs->events.set_title, &w->set_title);
    w->set_parent.notify = x11_set_parent;
    wl_signal_add(&xs->events.set_parent, &w->set_parent);
    w->request_move.notify = x11_request_move;
    wl_signal_add(&xs->events.request_move, &w->request_move);
    w->request_resize.notify = x11_request_resize;
    wl_signal_add(&xs->events.request_resize, &w->request_resize);
    w->x_request_maximize.notify = x11_request_maximize;
    wl_signal_add(&xs->events.request_maximize, &w->x_request_maximize);
    wl_list_insert(&s.windows, &w->link);
}

static void x11_ready(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    s.xwayland_up = true;
    wlr_xwayland_set_seat(s.xwayland, s.seat);
    if (wlr_xcursor_manager_load(s.xcursor, 1)) {
        struct wlr_xcursor *xc = wlr_xcursor_manager_get_xcursor(s.xcursor, "default", 1);
        if (xc != NULL && xc->image_count > 0) {
            struct wlr_xcursor_image *im = xc->images[0];
            wlr_xwayland_set_cursor(s.xwayland, im->buffer, im->width * 4, im->width, im->height,
                                    (int32_t)im->hotspot_x, (int32_t)im->hotspot_y);
        }
    }
    fprintf(stderr, "native: compositor: Xwayland started on DISPLAY=%s (an X11 program connected)\n",
            s.xwayland->display_name);
}

/* Xwayland, started when the first X11 program connects. */
static void xwayland_init(void)
{
    const char *bin = getenv("WLR_XWAYLAND");
    if (bin == NULL || bin[0] == '\0') bin = "/usr/bin/Xwayland";
    if (access(bin, X_OK) != 0) {
        unsetenv("DISPLAY");
        fprintf(stderr, "native: compositor: X11 programs cannot run here: Xwayland is not installed (%s; "
                        "sudo apt install xwayland). Wayland programs run as before\n", bin);
        return;
    }
    s.xwayland = wlr_xwayland_create(s.display, s.compositor, true);
    if (s.xwayland == NULL) {
        unsetenv("DISPLAY");
        fprintf(stderr, "native: compositor: X11 programs cannot run here: Xwayland did not set up\n");
        return;
    }
    s.xw_new_surface.notify = x11_new_surface;
    wl_signal_add(&s.xwayland->events.new_surface, &s.xw_new_surface);
    s.xw_ready.notify = x11_ready;
    wl_signal_add(&s.xwayland->events.ready, &s.xw_ready);
    setenv("DISPLAY", s.xwayland->display_name, 1);
    fprintf(stderr, "native: compositor: X11 programs on DISPLAY=%s (Xwayland starts when the first connects)\n",
            s.xwayland->display_name);
}
#else
static void xwayland_init(void)
{
    unsetenv("DISPLAY");
    fprintf(stderr, "native: compositor: X11 programs cannot run here: this wlroots has no Xwayland\n");
}
#endif

/* ----------------------------------------------------------- keyboard */

/* Everything for Family mruby's input goes through here (counted: a lock
 * must stop it all, L13). */
static void push_fmrb(uint32_t type, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    s.stats.to_fmrb++;
    native_input_push(type, a, b, c, d);
}

static void send_fmrb_key(uint32_t code, bool down)
{
    uint8_t usage = fmrb_linux_key_to_hid(code);
    if (usage == 0) return;
    push_fmrb(down ? NATIVE_INPUT_KEY_DOWN : NATIVE_INPUT_KEY_UP, usage, s.mods, 0, 0);
}

/* Where a key taken by the compositor itself went (its release goes nowhere). */
#define KEY_TAKEN (-2)

/* Super and an arrow (L13): the window operation, for the window in front
 * whoever has the keyboard. 0 when the key is not one. */
static int winop_key(uint32_t code)
{
    if (!native_winops_enabled() || !(s.super_held[0] || s.super_held[1])) return 0;
    switch (code) {
    case KEY_LEFT: return WINOPS_DO_LEFT;
    case KEY_RIGHT: return WINOPS_DO_RIGHT;
    case KEY_UP: return WINOPS_DO_MAX;
    case KEY_DOWN: return WINOPS_DO_RESTORE;
    default: return 0;
    }
}

static void lock_key(keyboard_t *k, struct wlr_keyboard_key_event *ev);

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
    /* On a console: Ctrl+Alt+F1..F12 go to another virtual terminal. */
    if (down && s.session != NULL && k->wlr->xkb_state != NULL) {
        const xkb_keysym_t *syms = NULL;
        int n = xkb_state_key_get_syms(k->wlr->xkb_state, code + 8, &syms);
        for (int i = 0; i < n; i++) {
            if (syms[i] >= XKB_KEY_XF86Switch_VT_1 && syms[i] <= XKB_KEY_XF86Switch_VT_12) {
                wlr_session_change_vt(s.session, syms[i] - XKB_KEY_XF86Switch_VT_1 + 1);
                return;
            }
        }
    }
    /* Family mruby's modifier bits follow every key, wherever it goes, so a
     * hotkey sent to Family mruby carries the modifiers held for a window. */
    uint8_t bit = fmrb_linux_key_mod_bit(code);
    if (bit != 0) {
        if (down) s.mods |= bit;
        else s.mods &= (uint8_t)~bit;
    }
    if (code == KEY_LEFTMETA) s.super_held[0] = down;
    if (code == KEY_RIGHTMETA) s.super_held[1] = down;
    /* Locked (L13): the lock surface's, nobody else's. */
    if (s.lk.locked) {
        lock_key(k, ev);
        return;
    }
    /* A key goes up where it went down, whoever has the keyboard now. The
     * IME (L12b) takes the keys while it serves the focused text input,
     * but for the switching hotkeys; the ones it does not use come back on
     * its own keyboard and go where the focus is. */
    int target = comp_route_focus(&s.route);
    int winop = down ? winop_key(code) : 0;
    if (winop != 0) target = KEY_TAKEN;
    else if (down && fmrb_hotkey(code)) target = COMP_FMRB;
    /* The volume keys (L14) are Family mruby's too: they move the
     * machine's volume (native/backend/input_native.c). */
    else if (down && (code == KEY_MUTE || code == KEY_VOLUMEUP || code == KEY_VOLUMEDOWN)) target = COMP_FMRB;
    else if (down && wl_ime_takes_keys(k->wlr)) target = KEY_TO_IME;
    if (code <= KEY_MAX) {
        if (down) k->key_target[code] = target;
        else target = k->key_target[code];
    }
    if (target == KEY_TAKEN) {
        if (winop != 0) {
            s.stats.winops++;
            native_input_push(NATIVE_INPUT_WINOP, (uint32_t)winop, 0, 0, 0);
        }
        return;
    }
    if (target == KEY_TO_IME) {
        wl_ime_key(k->wlr, ev->time_msec, code, ev->state);
        return;
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
    if (s.lk.locked) {
        if (s.lk.focus != NULL) {
            wlr_seat_set_keyboard(s.seat, k->wlr);
            wlr_seat_keyboard_notify_modifiers(s.seat, &k->wlr->modifiers);
        }
        return;
    }
    if (wl_ime_modifiers(k->wlr)) return;
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
    wl_list_for_each(k, &s.keyboards, link) {
        if (!k->own_keymap) set_keymap(k->wlr, layout);
    }
    fprintf(stderr, "native: compositor: the windows' keymap is %s (keyboard_layout)\n",
            layout == FMRB_KEYMAP_LAYOUT_JP ? "jp" : "us");
}

static void add_keyboard_as(struct wlr_keyboard *wkb, bool own_keymap)
{
    keyboard_t *k = calloc(1, sizeof(*k));
    if (k == NULL) return;
    k->wlr = wkb;
    k->own_keymap = own_keymap;
    if (!own_keymap) {
        set_keymap(wkb, s.layout_kb < 0 ? (int)fmrb_keymap_get_layout() : s.layout_kb);
        wlr_keyboard_set_repeat_info(wkb, 25, 600);
    }
    k->modifiers.notify = keyboard_modifiers;
    wl_signal_add(&wkb->events.modifiers, &k->modifiers);
    k->key.notify = keyboard_key;
    wl_signal_add(&wkb->events.key, &k->key);
    k->destroy.notify = keyboard_destroy;
    wl_signal_add(&wkb->base.events.destroy, &k->destroy);
    wl_list_insert(&s.keyboards, &k->link);
    if (!own_keymap) wlr_seat_set_keyboard(s.seat, wkb);
}

static void add_keyboard(struct wlr_keyboard *wkb)
{
    add_keyboard_as(wkb, false);
}

/* The IME's virtual keyboard (L12b): the keys it hands back, in its keymap. */
static void add_ime_keyboard(struct wlr_keyboard *wkb)
{
    add_keyboard_as(wkb, true);
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
    push_fmrb(NATIVE_INPUT_MOUSE_MOVE, (uint32_t)s.motion_x, (uint32_t)s.motion_y, 0, 0);
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
    /* X11 menus and tooltips first: they are over everything. */
    window_t *ow;
    wl_list_for_each(ow, &s.windows, link) {
        if (!is_or(ow) || !ow->mapped || ow->id <= 0) continue;
        struct wlr_scene_node *node = wlr_scene_node_at(&ow->tree->node, x, y, sx, sy);
        if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) continue;
        struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
        if (ss == NULL) continue;
        *surf = ss->surface;
        return ow->id;
    }
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
    struct wlr_box g = win_geometry(w);
    *sx = s.cursor->x - cw->x + g.x;
    *sy = s.cursor->y - cw->y + g.y;
    return win_surface(w);
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
            comp_route_to_frame(&s.route, (int)x, (int)y, &p.hit.x, &p.hit.y);
        }
    }
    if (p.hit.target != COMP_FMRB && p.surf == NULL) p.surf = surface_at(p.hit.target, &p.sx, &p.sy);
    return p;
}

/* Where the pointer is in frame coordinates, clamped. */
static void pointer_frame(int *fx, int *fy)
{
    comp_route_to_frame(&s.route, (int)s.cursor->x, (int)s.cursor->y, fx, fy);
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

static void lock_motion(uint32_t time_msec);

static void process_motion(uint32_t time_msec)
{
    if (s.lk.locked) {
        lock_motion(time_msec);
        return;
    }
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
        comp_route_set_cursor(&s.route, true, p.hit.x, p.hit.y);
        flush_motion(false);
        return;
    }
    fmrb_cursor(false);
    comp_route_set_cursor(&s.route, false, 0, 0);
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

/* The pointer stands still while what is under it changes (a window comes,
 * goes or moves; a hole opens or closes in a new frame): what it is over is
 * found again, as a move would (L9e). Nothing is sent when that has not
 * changed. Not while a button is held or a window is being moved. */
static void recheck_pointer(void)
{
    if (s.cursor == NULL || s.grab.mode != GRAB_NONE || s.buttons_held > 0 || s.lk.locked) return;
    ptr_hit_t p = pointer_hit();
    bool over = p.hit.target != COMP_FMRB;
    if (!over && !s.fmrb_cursor_hidden) return;
    if (over && s.fmrb_cursor_hidden && p.hit.target == s.ptr_id &&
        s.seat->pointer_state.focused_surface == p.surf) {
        return;
    }
    s.stats.pointer_rechecks++;
    process_motion((uint32_t)now_ms());
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
    push_fmrb(NATIVE_INPUT_MOUSE_BUTTON, (uint32_t)fx, (uint32_t)fy, (uint32_t)b, down ? 1 : 0);
}

#if WLR_HAS_XWAYLAND
static bool x11_is_menu(struct wlr_xwayland_surface *xs)
{
    return wlr_xwayland_surface_has_window_type(xs, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_POPUP_MENU) ||
           wlr_xwayland_surface_has_window_type(xs, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DROPDOWN_MENU) ||
           wlr_xwayland_surface_has_window_type(xs, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_MENU) ||
           wlr_xwayland_surface_has_window_type(xs, WLR_XWAYLAND_NET_WM_WINDOW_TYPE_COMBO);
}
#endif

/* An X11 menu holds the pointer in X (a grab) and closes on a press
 * anywhere else -- but Xwayland only sees what lands on its own surfaces, so
 * a press on Family mruby or on a Wayland window never reached it (L9c).
 * Such a press is passed to it too (L9e): through the menu's surface, at
 * the press's place (outside the menu), down and up; the X server hands it
 * to the program holding the grab. Only windows that say they are menus
 * (their window type): a tooltip holds no grab, and a press passed through
 * it would land on whatever X11 window is at that place. Not for a press on
 * an X11 window, which Xwayland sees itself. */
static void press_x11_menus(int target, uint32_t time_msec, uint32_t button)
{
#if WLR_HAS_XWAYLAND
    if (target != COMP_FMRB) {
        window_t *tw = window_by_id(target);
        if (tw != NULL && tw->kind == WIN_X11) return;
    }
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (!is_or(w) || !w->mapped || w->id <= 0 || w->xs->surface == NULL || !x11_is_menu(w->xs)) continue;
        int nx = 0, ny = 0;
        wlr_scene_node_coords(&w->tree->node, &nx, &ny);
        double sx = s.cursor->x - nx, sy = s.cursor->y - ny;
        wlr_seat_pointer_notify_enter(s.seat, w->xs->surface, sx, sy);
        wlr_seat_pointer_notify_motion(s.seat, time_msec, sx, sy);
        wlr_seat_pointer_notify_button(s.seat, time_msec, button, WL_POINTER_BUTTON_STATE_PRESSED);
        wlr_seat_pointer_notify_button(s.seat, time_msec, button, WL_POINTER_BUTTON_STATE_RELEASED);
        wlr_seat_pointer_notify_frame(s.seat);
        wlr_seat_pointer_clear_focus(s.seat);
        s.stats.x11_menu_presses++;
        fprintf(stderr, "native: compositor: a press elsewhere passed to X11 menu %d (%.0f,%.0f of it)\n", w->id, sx,
                sy);
        /* One is enough: the grab's owner gets it whichever surface it came through. */
        return;
    }
#else
    (void)target;
    (void)time_msec;
    (void)button;
#endif
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
    if (s.lk.locked) {
        /* The lock surface's, when it has the pointer (L13). */
        if (s.seat->pointer_state.focused_surface != NULL) {
            wlr_seat_pointer_notify_button(s.seat, ev->time_msec, ev->button, ev->state);
        }
        return;
    }
    ptr_hit_t p;
    if (down && s.buttons_held == 0) {
        p = pointer_hit();
        s.pointer_fmrb_too = false;
        if (p.popup) {
            /* A popup is the window's own business: no focus change. */
            s.pointer_target = p.hit.target;
        } else {
            /* Menus close on a press outside them (but on their own window,
             * which closes them itself as it sees fit). */
            close_popups(comp_route_at(&s.route, (int)s.cursor->x, (int)s.cursor->y).target);
            press_x11_menus(comp_route_at(&s.route, (int)s.cursor->x, (int)s.cursor->y).target, ev->time_msec,
                            ev->button);
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
    if (s.lk.locked) {
        if (s.seat->pointer_state.focused_surface != NULL) {
            wlr_seat_pointer_notify_axis(s.seat, ev->time_msec, ev->orientation, ev->delta, ev->delta_discrete,
                                         ev->source, ev->relative_direction);
        }
        return;
    }
    ptr_hit_t p = pointer_hit();
    if (p.hit.target == COMP_FMRB) {
        if (ev->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
        /* Wayland's positive is towards the user; the system's is away. */
        int notches = ev->delta_discrete != 0 ? -ev->delta_discrete / WLR_POINTER_AXIS_DISCRETE_STEP
                                              : (ev->delta > 0 ? -1 : (ev->delta < 0 ? 1 : 0));
        if (notches == 0) return;
        flush_motion(true);
        push_fmrb(NATIVE_INPUT_MOUSE_WHEEL, (uint32_t)p.hit.x, (uint32_t)p.hit.y,
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
    if (s.mode_w > 0) {
        /* display.mode: that mode when the output lists it, else a custom
         * one (the headless and nested outputs take any size). */
        struct wlr_output_mode *m, *pick = NULL;
        wl_list_for_each(m, &out->modes, link) {
            if (m->width == s.mode_w && m->height == s.mode_h &&
                (s.mode_hz == 0 || (m->refresh + 500) / 1000 == s.mode_hz)) {
                pick = m;
                break;
            }
        }
        if (pick != NULL) wlr_output_state_set_mode(&st, pick);
        else wlr_output_state_set_custom_mode(&st, s.mode_w, s.mode_h, s.mode_hz * 1000);
    } else if (mode != NULL) {
        wlr_output_state_set_mode(&st, mode);
    }
    /* display.rotate turns the output clockwise, as wlr-randr --transform
     * did in the cage session (L7); the pointer turns with it. */
    static const enum wl_output_transform turns[] = { WL_OUTPUT_TRANSFORM_NORMAL, WL_OUTPUT_TRANSFORM_90,
                                                      WL_OUTPUT_TRANSFORM_180, WL_OUTPUT_TRANSFORM_270 };
    int turn = (s.rotate / 90) & 3;
    if (turn != 0) wlr_output_state_set_transform(&st, turns[turn]);
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
    fprintf(stderr, "native: compositor: output %s %dx%d, rotate %d\n", out->name, out->width, out->height,
            s.rotate);
    relayout();
}

/* ------------------------------------------------- the screen lock (L13) */

/* ext-session-lock-v1: a lock program (swaylock, compositor.locker) asks to
 * lock; the screen is covered at once (a plain colour), and the program's
 * lock surface goes over it. While locked, nothing reaches Family mruby or a
 * Linux window: keys and the pointer go to the lock surface alone (the
 * virtual-terminal switch stays the machine's). The
 * program unlocks when it is satisfied (it checks the password; Family mruby
 * never sees one). A program that goes away without unlocking leaves the
 * screen locked, as the protocol says: the cover stays, and a key (or
 * logind's Lock, or the power menu's) starts the program again. */

/* The used output's lock surface takes the keyboard and the pointer (the
 * others are not shown). */
static bool lock_primary(const lock_surface_t *ls)
{
    return ls->wlr->output == s.output;
}

static void lock_cover_size(void)
{
    if (s.lk.cover == NULL) return;
    struct wlr_box box;
    wlr_output_layout_get_box(s.layout, NULL, &box);
    wlr_scene_node_set_position(&s.lk.cover->node, box.x, box.y);
    wlr_scene_rect_set_size(s.lk.cover, box.width, box.height);
}

static void lock_relayout(void)
{
    lock_cover_size();
    lock_surface_t *ls;
    wl_list_for_each(ls, &s.lk.surfaces, link) {
        int ew = 0, eh = 0;
        if (ls->wlr->output != NULL) wlr_output_effective_resolution(ls->wlr->output, &ew, &eh);
        if (ew > 0 && eh > 0) wlr_session_lock_surface_v1_configure(ls->wlr, (uint32_t)ew, (uint32_t)eh);
    }
}

static void lock_publish(void)
{
    comp_lock_state_t st = s.lk.locked ? COMP_LOCK_LOCKED : (s.lk.awaiting ? COMP_LOCK_LOCKING : COMP_LOCK_NONE);
    comp_link_set_lock(s.lk.available, st);
}

/* Locked or not: the cover, and the keyboard and the pointer taken from (or
 * given back to) Family mruby and the windows. */
static void lock_set(bool on)
{
    if (on == s.lk.locked) return;
    s.lk.locked = on;
    wlr_scene_node_set_enabled(&s.lock_tree->node, on);
    if (on) {
        lock_cover_size();
        wlr_seat_keyboard_notify_clear_focus(s.seat);
        wlr_seat_pointer_clear_focus(s.seat);
        wl_ime_focus(NULL, false);
        s.buttons_held = 0;
        s.pointer_target = COMP_FMRB;
        s.grab.mode = GRAB_NONE;
        s.motion_pending = false;
        wlr_cursor_set_xcursor(s.cursor, s.xcursor, "default");
        fmrb_cursor(false);
    } else {
        s.lk.focus = NULL;
        apply_focus();
        process_motion((uint32_t)now_ms());
    }
    lock_publish();
}

static void lock_focus(struct wlr_surface *surf)
{
    s.lk.focus = surf;
    if (surf == NULL) {
        wlr_seat_keyboard_notify_clear_focus(s.seat);
        return;
    }
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(s.seat);
    if (kb != NULL) {
        wlr_seat_keyboard_notify_enter(s.seat, surf, kb->keycodes, kb->num_keycodes, &kb->modifiers);
    } else {
        wlr_seat_keyboard_notify_enter(s.seat, surf, NULL, 0, NULL);
    }
}

/* Whether the first word of a command is a program found on PATH. */
static bool program_found(const char *cmd)
{
    char first[256];
    if (sscanf(cmd, "%255s", first) != 1) return false;
    if (strchr(first, '/') != NULL) return access(first, X_OK) == 0;
    const char *path = getenv("PATH");
    if (path == NULL) path = "/usr/local/bin:/usr/bin:/bin";
    while (*path) {
        size_t n = strcspn(path, ":");
        if (n > 0 && n < 512) {
            char full[800];
            snprintf(full, sizeof(full), "%.*s/%s", (int)n, path, first);
            if (access(full, X_OK) == 0) return true;
        }
        path += n;
        if (*path == ':') path++;
    }
    return false;
}

/* Start the lock program (compositor.locker), unless it runs. */
static void lock_start(const char *why)
{
    if (!s.lk.available) {
        fprintf(stderr, "native: compositor: lock asked (%s), but there is no lock program (%s)\n", why,
                s.lk.program[0] ? s.lk.program : "compositor.locker is empty");
        return;
    }
    if (s.lk.pid > 0) return;
    if (s.lk.locked && s.lk.lock != NULL) return;
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "native: compositor: lock program: fork: %s\n", strerror(errno));
        return;
    }
    if (pid == 0) {
        /* Async-signal-safe calls only: signals as new, a session of its
         * own, none of the program's descriptors (the control socket's
         * client would wait for this child to close it). */
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        for (int sig = 1; sig < NSIG; sig++) signal(sig, SIG_DFL);
        setsid();
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        for (int fd = 3; fd < 4096; fd++) close(fd);
        execl("/bin/sh", "sh", "-c", s.lk.program, (char *)NULL);
        _exit(127);
    }
    s.lk.pid = pid;
    s.lk.awaiting = true;
    s.lk.started_ms = now_ms();
    s.lk.starts++;
    fprintf(stderr, "native: compositor: started the lock program \"%s\" (pid %d; %s)\n", s.lk.program, (int)pid, why);
    lock_publish();
}

static void lock_reap(void)
{
    if (s.lk.pid <= 0) return;
    int st = 0;
    if (waitpid(s.lk.pid, &st, WNOHANG) != s.lk.pid) return;
    fprintf(stderr, "native: compositor: the lock program ended (%d)%s\n",
            WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st),
            s.lk.locked && s.lk.lock == NULL ? "; the screen stays locked" : "");
    s.lk.pid = 0;
    s.lk.awaiting = false;
    lock_publish();
}

/* A key while locked: the lock surface's, or (the program gone) a new start. */
static void lock_key(keyboard_t *k, struct wlr_keyboard_key_event *ev)
{
    bool down = ev->state == WL_KEYBOARD_KEY_STATE_PRESSED;
    if (s.lk.focus != NULL) {
        wlr_seat_set_keyboard(s.seat, k->wlr);
        wlr_seat_keyboard_notify_key(s.seat, ev->time_msec, ev->keycode, ev->state);
        return;
    }
    s.lk.keys_held_back++;
    if (down && s.lk.lock == NULL && s.lk.pid <= 0 && now_ms() - s.lk.started_ms >= 2000) {
        lock_start("a key, the lock program being gone");
    }
}

/* The pointer while locked: the first output's lock surface only (it lies
 * at the output's origin, the layout's). */
static void lock_motion(uint32_t time_msec)
{
    lock_surface_t *ls;
    wl_list_for_each(ls, &s.lk.surfaces, link) {
        if (!lock_primary(ls) || !ls->wlr->surface->mapped) continue;
        wlr_seat_pointer_notify_enter(s.seat, ls->wlr->surface, s.cursor->x, s.cursor->y);
        wlr_seat_pointer_notify_motion(s.seat, time_msec, s.cursor->x, s.cursor->y);
        return;
    }
    wlr_seat_pointer_clear_focus(s.seat);
}

static void lock_surface_map(struct wl_listener *l, void *data)
{
    (void)data;
    lock_surface_t *ls = wl_container_of(l, ls, map);
    if (!lock_primary(ls)) return;
    lock_focus(ls->wlr->surface);
    lock_motion((uint32_t)now_ms());
    if (s.lk.lock != NULL && !s.lk.sent_locked) {
        wlr_session_lock_v1_send_locked(s.lk.lock);
        s.lk.sent_locked = true;
        fprintf(stderr, "native: compositor: the screen is locked (the lock program's surface is up)\n");
    }
}

static void lock_surface_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    lock_surface_t *ls = wl_container_of(l, ls, destroy);
    if (s.lk.focus == ls->wlr->surface) lock_focus(NULL);
    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->destroy.link);
    wl_list_remove(&ls->link);
    free(ls);
}

static void lock_new_surface(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_session_lock_surface_v1 *wls = data;
    lock_surface_t *ls = calloc(1, sizeof(*ls));
    if (ls == NULL) return;
    ls->wlr = wls;
    ls->tree = wlr_scene_subsurface_tree_create(s.lock_tree, wls->surface);
    int ox = 0, oy = 0;
    if (wls->output != NULL) {
        struct wlr_box b;
        wlr_output_layout_get_box(s.layout, wls->output, &b);
        ox = b.x;
        oy = b.y;
    }
    wlr_scene_node_set_position(&ls->tree->node, ox, oy);
    wlr_scene_node_set_enabled(&ls->tree->node, lock_primary(ls));
    ls->map.notify = lock_surface_map;
    wl_signal_add(&wls->surface->events.map, &ls->map);
    ls->destroy.notify = lock_surface_destroy;
    wl_signal_add(&wls->events.destroy, &ls->destroy);
    wl_list_insert(&s.lk.surfaces, &ls->link);
    int ew = 0, eh = 0;
    if (wls->output != NULL) wlr_output_effective_resolution(wls->output, &ew, &eh);
    wlr_session_lock_surface_v1_configure(wls, (uint32_t)(ew > 0 ? ew : 1), (uint32_t)(eh > 0 ? eh : 1));
}

static void lock_forget(void)
{
    wl_list_remove(&s.lk.lock_unlock.link);
    wl_list_remove(&s.lk.lock_destroy.link);
    wl_list_remove(&s.lk.lock_new_surface.link);
    s.lk.lock = NULL;
}

static void lock_unlock(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    s.lk.unlocks++;
    fprintf(stderr, "native: compositor: the lock program unlocked the screen\n");
    lock_forget();
    lock_set(false);
}

static void lock_destroy(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    lock_forget();
    if (s.lk.locked) {
        s.lk.died++;
        s.lk.focus = NULL;
        wlr_seat_keyboard_notify_clear_focus(s.seat);
        wlr_seat_pointer_clear_focus(s.seat);
        fprintf(stderr, "native: compositor: the lock went away without unlocking: the screen stays locked "
                        "(a key, or logind's Lock, starts the lock program again)\n");
    }
    lock_publish();
}

static void new_lock(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_session_lock_v1 *lock = data;
    if (s.lk.lock != NULL) {
        fprintf(stderr, "native: compositor: a second lock refused (one is there)\n");
        wlr_session_lock_v1_destroy(lock);
        return;
    }
    s.lk.lock = lock;
    s.lk.awaiting = false;
    s.lk.sent_locked = false;
    s.lk.locks++;
    s.lk.lock_unlock.notify = lock_unlock;
    wl_signal_add(&lock->events.unlock, &s.lk.lock_unlock);
    s.lk.lock_destroy.notify = lock_destroy;
    wl_signal_add(&lock->events.destroy, &s.lk.lock_destroy);
    s.lk.lock_new_surface.notify = lock_new_surface;
    wl_signal_add(&lock->events.new_surface, &s.lk.lock_new_surface);
    fprintf(stderr, "native: compositor: locking the screen (a lock program asked)\n");
    lock_set(true);
}

static void lock_init(const native_machine_t *m)
{
    wl_list_init(&s.lk.surfaces);
    snprintf(s.lk.program, sizeof(s.lk.program), "%s", m->compositor.locker);
    s.lk.available = s.lk.program[0] != '\0' && program_found(s.lk.program);
    s.lock_tree = wlr_scene_tree_create(&s.scene->tree);
    static const float cover[4] = { 0.10f, 0.11f, 0.14f, 1.0f };
    s.lk.cover = wlr_scene_rect_create(s.lock_tree, 0, 0, cover);
    wlr_scene_node_set_enabled(&s.lock_tree->node, false);
    s.lk.mgr = wlr_session_lock_manager_v1_create(s.display);
    s.lk.new_lock.notify = new_lock;
    wl_signal_add(&s.lk.mgr->events.new_lock, &s.lk.new_lock);
    if (s.lk.available) {
        fprintf(stderr, "native: compositor: the screen lock: \"%s\" (ext-session-lock)\n", s.lk.program);
    } else if (s.lk.program[0] != '\0') {
        fprintf(stderr, "native: compositor: the screen lock: %s is not installed (no Lock entry; "
                        "ext-session-lock is served for any lock program)\n", s.lk.program);
    }
    lock_publish();
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
 *   comp info            "OK out=WxH frame=WxH scale=S at=X,Y
 *                        view=VX,VY,VWxVH vscale=S vat=X,Y focus=F
 *                        windows=ID:WxH+X+Y/FXxFY+FWxFH/V,... ptr=ID:SX,SY
 *                        cursor=fmrb|window keymap=us|jp socket=NAME
 *                        renderer=R": each window's size and place on the
 *                        output, its content rectangle in frame coordinates
 *                        and whether it shows (V 1/0), bottom to top; the
 *                        last pointer position sent to a window
 *                        (surface-local, output pixels)
 *   comp stats [reset]   pictures (count, mean and longest gap), output
 *                        commits, keys sent to a window and the time from
 *                        each to the window's next commit, and how often
 *                        the shown part of the frame changed with a frame
 *                        (a fullscreen app's view, L9e), since the last
 *                        reset
 *   comp key CODE 1|0    a Linux key code down / up, through the seat
 *   comp move X Y        the pointer to output coordinates
 *   comp button B 1|0    1 left, 2 middle, 3 right
 *   comp click X Y       move, then left down and up
 *   comp wheel N         notches, + away from the user
 *   comp apps LANG       the launcher's Linux list (linux_apps.h) as LANG
 *                        names it: "OK ready=0|1 count=N"
 *   comp app ID LANG     one entry of it: "OK index=K name=NAME icon=PATH"
 *                        (K from 0 in the list's order, -1 when not listed;
 *                        icon "-" for none)
 *
 * The tools' keyboard and pointer go through the same handlers (focus,
 * routing) as the backend's devices.
 */
/* comp apps / comp app (see wlr_ctl). */
static void apps_ctl(const char *a, char *out, size_t cap)
{
    char id[256] = "", lang[16] = "en";
    bool one = strncmp(a, "app ", 4) == 0;
    if (one) {
        if (sscanf(a + 4, "%255s %15s", id, lang) < 1) {
            snprintf(out, cap, "ERR comp app ID LANG\n");
            return;
        }
    } else {
        sscanf(a + 4, " %15s", lang);
    }
    int32_t need = linux_apps_list(LINUX_APPS_LIST, lang, NULL, 0);
    char *buf = need > 0 ? malloc((size_t)need + 1) : NULL;
    int32_t got = buf ? linux_apps_list(LINUX_APPS_LIST, lang, buf, (size_t)need) : -1;
    if (got < 0 || got > need) {
        snprintf(out, cap, "ERR no Linux apps here\n");
        free(buf);
        return;
    }
    buf[got] = '\0';
    bool ready = strncmp(buf, "ready\n", 6) == 0;
    int count = 0, index = -1;
    char name[160] = "", icon[300] = "-";
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);   /* the header */
    for (line = strtok_r(NULL, "\n", &save); line != NULL; line = strtok_r(NULL, "\n", &save), count++) {
        char *t1 = strchr(line, '\t');
        char *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
        if (t1 == NULL || t2 == NULL || !one || index >= 0) continue;
        *t1 = *t2 = '\0';
        if (strcmp(t1 + 1, id) != 0) continue;
        index = count;
        snprintf(name, sizeof(name), "%s", line);
        if (t2[1] != '\0') snprintf(icon, sizeof(icon), "%s", t2 + 1);
    }
    free(buf);
    if (one) {
        snprintf(out, cap, "OK index=%d name=%s icon=%s\n", index, name, icon);
    } else {
        snprintf(out, cap, "OK ready=%d count=%d\n", ready ? 1 : 0, count);
    }
}

static bool wlr_ctl(const char *req, char *out, size_t cap)
{
    if (strncmp(req, "comp ", 5) != 0 && strcmp(req, "comp") != 0) return false;
    const char *a = req + 4;
    while (*a == ' ') a++;
    int x = 0, y = 0;
    unsigned code = 0;
    if (strcmp(a, "info") == 0 || a[0] == '\0') {
        const comp_route_t *r = &s.route;
        int n = snprintf(out, cap, "OK out=%dx%d frame=%dx%d scale=%d at=%d,%d view=%d,%d,%dx%d vscale=%d vat=%d,%d "
                         "focus=%d windows=",
                         r->out_w, r->out_h, r->fb_w, r->fb_h, r->place.scale, r->place.x, r->place.y,
                         r->viewing ? r->view_x : 0, r->viewing ? r->view_y : 0, r->viewing ? r->view_w : r->fb_w,
                         r->viewing ? r->view_h : r->fb_h, r->show.scale, r->show.x, r->show.y, r->focus);
        for (int i = 0; i < r->n && n > 0 && (size_t)n < cap; i++) {
            const comp_window_t *w = &r->win[i];
            n += snprintf(out + n, cap - (size_t)n, "%s%d:%dx%d+%d+%d/%dx%d+%dx%d/%d", i ? "," : "", w->id, w->w,
                          w->h, w->x, w->y, w->fx, w->fy, w->fw, w->fh, w->visible ? 1 : 0);
        }
        if (n > 0 && (size_t)n < cap) {
            n += snprintf(out + n, cap - (size_t)n, " ptr=%d:%.2f,%.2f cursor=%s keymap=%s socket=%s renderer=%s x11=",
                          s.ptr_id, s.ptr_sx, s.ptr_sy, s.fmrb_cursor_hidden ? "window" : "fmrb",
                          s.layout_kb == FMRB_KEYMAP_LAYOUT_JP ? "jp" : "us", s.socket, renderer_name());
        }
        /* The X11 windows' ids (menus marked m), and Xwayland: off (none
         * here), lazy (not started yet) or up. */
        const char *xw = "off";
        int k = 0;
#if WLR_HAS_XWAYLAND
        window_t *w;
        wl_list_for_each_reverse(w, &s.windows, link) {
            if (w->kind != WIN_X11 || !w->mapped || w->id <= 0 || n <= 0 || (size_t)n >= cap) continue;
            n += snprintf(out + n, cap - (size_t)n, "%s%d%s", k++ ? "," : "", w->id, w->or_win ? "m" : "");
        }
        if (s.xwayland != NULL) xw = s.xwayland_up ? "up" : "lazy";
#endif
        if (n > 0 && (size_t)n < cap) {
            n += snprintf(out + n, cap - (size_t)n, "%s xwayland=%s rotate=%d", k ? "" : "-", xw, s.rotate);
        }
        /* L13: how each window is laid out (m maximized, l / r a half, f
         * - as placed), the lock. */
        if (n > 0 && (size_t)n < cap) n += snprintf(out + n, cap - (size_t)n, " states=");
        for (int i = 0; i < r->n && n > 0 && (size_t)n < cap; i++) {
            const comp_window_t *cw = &r->win[i];
            const char *st = cw->state == COMP_STATE_MAX ? "m" : cw->state == COMP_STATE_LEFT ? "l"
                           : cw->state == COMP_STATE_RIGHT ? "r" : "-";
            n += snprintf(out + n, cap - (size_t)n, "%s%d:%s", i ? "," : "", cw->id, st);
        }
        if (n > 0 && (size_t)n < cap && r->n == 0) n += snprintf(out + n, cap - (size_t)n, "-");
        const char *lk = s.lk.locked ? (s.lk.lock != NULL ? "locked" : "dead") : (s.lk.awaiting ? "locking" : "none");
        if (n > 0 && (size_t)n < cap) {
            n += snprintf(out + n, cap - (size_t)n, " lock=%s locker=%s", lk, s.lk.available ? "yes" : "no");
        }
        if (n > 0 && (size_t)n < cap) snprintf(out + n, cap - (size_t)n, "\n");
        return true;
    }
    if (strncmp(a, "stats", 5) == 0) {
        uint64_t t = now_ms();
        double span = (double)(t - s.stats.since_ms) / 1000.0;
        unsigned gaps = s.stats.pictures > 1 ? s.stats.pictures - 1 : 0;
        snprintf(out, cap,
                 "OK span_s=%.1f pictures=%u gap_mean_ms=%.1f gap_max_ms=%llu commits=%u keys=%u "
                 "key_commit_n=%u key_commit_mean_ms=%.1f key_commit_max_ms=%llu view_changes=%u pointer_rechecks=%u "
                 "x11_menu_presses=%u to_fmrb=%u winops=%u locks=%u unlocks=%u lock_died=%u "
                 "lock_keys_held=%u lock_starts=%u\n",
                 span, s.stats.pictures,
                 gaps ? (double)s.stats.picture_gap_sum_ms / gaps : 0.0,
                 (unsigned long long)s.stats.picture_gap_max_ms, s.stats.commits, s.stats.keys,
                 s.stats.key_lat_n, s.stats.key_lat_n ? (double)s.stats.key_lat_sum_ms / s.stats.key_lat_n : 0.0,
                 (unsigned long long)s.stats.key_lat_max_ms, s.stats.view_changes, s.stats.pointer_rechecks,
                 s.stats.x11_menu_presses, s.stats.to_fmrb, s.stats.winops, s.lk.locks,
                 s.lk.unlocks, s.lk.died, s.lk.keys_held_back, s.lk.starts);
        if (strcmp(a, "stats reset") == 0) {
            memset(&s.stats, 0, sizeof(s.stats));
            s.stats.since_ms = t;
        }
        return true;
    }
    if (strcmp(a, "ime") == 0) {
        /* The IME (L12b): whom it serves, the composing text, the popups. */
        wl_ime_describe(out, cap);
        return true;
    }
    if (strcmp(a, "clip") == 0) {
        /* The clipboard (L12a): who owns the selection and the counts. */
        wl_clip_stats_t c;
        wl_clip_stats(&c);
        native_clip_stats_t k;
        native_clip_stats(&k);
        snprintf(out, cap,
                 "OK owner=%s kept=%zu gave=%u last_gave=%zu served=%u linux_sets=%u took=%u last_took=%zu "
                 "refused=%u not_text=%u timeouts=%u owned=%u kept_again=%u fmrb_out=%u fmrb_taken=%u\n",
                 c.owner == 0 ? "none" : c.owner == 1 ? "fmrb" : "linux", c.kept_len, c.gave, c.last_gave,
                 c.served, c.linux_sets, c.took, c.last_took, c.refused, c.not_text, c.timeouts, c.owned,
                 c.kept, k.out, k.taken);
        return true;
    }
    if (strncmp(a, "apps", 4) == 0 || strncmp(a, "app ", 4) == 0) {
        apps_ctl(a, out, cap);
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
        snprintf(out, cap, "ERR comp: info, stats [reset], clip, key CODE 1|0, move X Y, button B 1|0, click X Y, wheel N\n");
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

/* The session's programs started by D-Bus or systemd (the desktop portal
 * and its wlroots backend, L14: a browser's screen sharing) learn where
 * the screen is: WAYLAND_DISPLAY (and DISPLAY, Xwayland's) and the desktop
 * name the portal picks its backends by (family-mruby-portals.conf). On
 * the console only (the login screen's session), or when the checks ask
 * (FMRB_COMPOSITOR_ACTIVATION_ENV=1): nested in another desktop, its
 * session's environment is that desktop's, not ours to change. The tool
 * runs on a thread of its own and is waited for there. */
static void *activation_env_run(void *arg)
{
    char **argv = arg;
    pid_t pid = 0;
    extern char **environ;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addclosefrom_np(&fa, 3);
    int rc = posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    int status = 0;
    if (rc == 0) waitpid(pid, &status, 0);
    fprintf(stderr, "native: compositor: the session's activation environment: %s%s\n",
            rc != 0 ? "dbus-update-activation-environment is missing" :
            (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? "WAYLAND_DISPLAY and XDG_CURRENT_DESKTOP" : "not updated",
            rc == 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 && getenv("DISPLAY") ? " (and DISPLAY)" : "");
    for (char **a = argv; *a; a++) free(*a);
    free(argv);
    return NULL;
}

static void activation_env(void)
{
    const char *ask = getenv("FMRB_COMPOSITOR_ACTIVATION_ENV");
    if (s.session == NULL && !(ask != NULL && strcmp(ask, "1") == 0)) return;
    char **argv = calloc(8, sizeof(char *));
    if (argv == NULL) return;
    int n = 0;
    char buf[160];
    argv[n++] = strdup("dbus-update-activation-environment");
    argv[n++] = strdup("--systemd");
    snprintf(buf, sizeof(buf), "WAYLAND_DISPLAY=%s", s.socket);
    argv[n++] = strdup(buf);
    snprintf(buf, sizeof(buf), "XDG_CURRENT_DESKTOP=%s", DESKTOP_NAME);
    argv[n++] = strdup(buf);
    const char *d = getenv("DISPLAY");
    if (d != NULL && d[0] != '\0') {
        snprintf(buf, sizeof(buf), "DISPLAY=%s", d);
        argv[n++] = strdup(buf);
    }
    argv[n] = NULL;
    pthread_t t;
    if (pthread_create(&t, NULL, activation_env_run, argv) == 0) {
        pthread_detach(t);
    } else {
        for (char **a = argv; *a; a++) free(*a);
        free(argv);
    }
}

/* Qt programs look as they do in Plasma (L9e). Qt picks its platform theme
 * from XDG_CURRENT_DESKTOP, which is ours (family-mruby: the launcher's
 * OnlyShowIn / NotShowIn rules and the programs' own see that name), so it
 * never picks Plasma's. Where Plasma's is installed and nothing chose
 * otherwise, it is named outright for every program started from here (the
 * launcher, the autostart, the Shell's "!", Xwayland's). */
static void qt_look(void)
{
    const char *have = getenv("QT_QPA_PLATFORMTHEME");
    if (have != NULL && have[0] != '\0') {
        fprintf(stderr, "native: compositor: Qt's look: QT_QPA_PLATFORMTHEME=%s (as set)\n", have);
        return;
    }
    char found[512];
    if (linux_apps_plasma_qt("", found, sizeof(found))) {
        setenv("QT_QPA_PLATFORMTHEME", "kde", 1);
        fprintf(stderr, "native: compositor: Qt's look: Plasma's (QT_QPA_PLATFORMTHEME=kde; %s)\n", found);
    }
}

/* The clipboard's wake (native_clip.h): from the task that copied. */
static void clip_wake(void)
{
    int fd = s.wake_fd;
    if (fd < 0) return;
    uint64_t one = 1;
    ssize_t n = write(fd, &one, sizeof(one));
    (void)n;
}

static bool wlr_open(const native_machine_t *m, char *why, size_t why_len)
{
    memset(&s, 0, sizeof(s));
    s.wake_fd = -1;
    s.layout_kb = -1;
    comp_route_init(&s.route);
    wl_list_init(&s.windows);
    wl_list_init(&s.popups);
    wl_list_init(&s.keyboards);
    snprintf(s.autostart, sizeof(s.autostart), "%s", m->compositor.autostart);
    s.rotate = m->display.rotate;
    if (strcmp(m->display.mode, "auto") != 0 &&
        sscanf(m->display.mode, "%dx%d@%d", &s.mode_w, &s.mode_h, &s.mode_hz) < 2) {
        snprintf(why, why_len, "display.mode \"%s\" is not auto, WxH or WxH@Hz", m->display.mode);
        return false;
    }
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
    s.backend = wlr_backend_autocreate(s.loop, &s.session);
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
    s.compositor = wlr_compositor_create(s.display, 5, s.renderer);
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
    /* The IME's candidates and Family mruby's composing text over all. */
    s.ime_tree = wlr_scene_tree_create(&s.scene->tree);
    s.ime = strcmp(m->input.ime, "wayland") == 0;

    s.new_output.notify = new_output;
    wl_signal_add(&s.backend->events.new_output, &s.new_output);
    /* Version 5: the window is told which window-manager features there
     * are (maximize, L13; no fullscreen, minimize or window menu). */
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
    if (s.ime) {
        static const wl_ime_host_t ime_host = { .surface_origin = surface_origin, .add_keyboard = add_ime_keyboard };
        wl_ime_init(s.display, s.seat, s.ime_tree, &s.route, &ime_host);
    }

    /* The screen lock over everything (L13). */
    lock_init(m);
    wlr_screencopy_manager_v1_create(s.display);
    /* The screen's capture for xdg-desktop-portal-wlr (L14: sharing the
     * screen from a browser's meeting, a recorder): it takes an output
     * through ext-image-copy-capture, or wlr-screencopy only with dmabuf,
     * which the pixman renderer (a virtual machine) does not give. */
    wlr_ext_image_copy_capture_manager_v1_create(s.display, 1);
    wlr_ext_output_image_capture_source_manager_v1_create(s.display, 1);
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
     * program's own children, and the Shell's "!" programs, see this one
     * (and this one's Xwayland, never the parent's X server). */
    setenv("WAYLAND_DISPLAY", s.socket, 1);
    qt_look();
    xwayland_init();
    if (s.ime) wl_ime_start(m->compositor.ime, s.socket);
    activation_env();

    s.wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (s.wake_fd >= 0) {
        s.wake_source = wl_event_loop_add_fd(s.loop, s.wake_fd, WL_EVENT_READABLE, on_wake, NULL);
        native_frame_set_wake_fd(s.wake_fd);
    }
    comp_link_attach(&s.route, s.wake_fd);
    wl_clip_init(s.display, s.loop, s.seat);
    native_clip_install(clip_wake);
    fprintf(stderr, "native: compositor: wlroots %s on WAYLAND_DISPLAY=%s (backends %s, renderer %s%s)\n",
            WLR_VERSION_STR, s.socket, getenv("WLR_BACKENDS") ? getenv("WLR_BACKENDS") : "auto",
            renderer_name(), s.session != NULL ? ", seat from the session" : "");
    /* The launcher's Linux applications (L9b): icons into the cache the
     * file namespace shows at LINUX_ICON_DIR. */
    char icon_dir[512];
    if (fmrb_hal_file_resolve_path(LINUX_ICON_DIR, icon_dir, sizeof(icon_dir)) == FMRB_OK) {
        linux_apps_config_t ac = { .icon_dir = icon_dir,
                                   .icon_fm_dir = LINUX_ICON_DIR,
                                   .desktop_name = DESKTOP_NAME,
                                   .mime_desktops = m->compositor.mime_desktops };
        linux_apps_start(&ac);
    } else {
        fprintf(stderr, "native: compositor: no place for the Linux icons (%s); no Linux apps\n", LINUX_ICON_DIR);
    }
    start_child();
    /* logind's Lock and PrepareForSleep lock the screen (L13). */
    if (s.lk.available) linux_logind_watch_start();
    return true;
}

/* The output as the picture sees it (turned by display.rotate): the size the
 * screen form "output" divides (main_native.c). */
static bool wlr_size(int *w, int *h)
{
    if (s.output == NULL) return false;
    wlr_output_effective_resolution(s.output, w, h);
    return *w > 0 && *h > 0;
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
    wl_clip_pump();
    wl_ime_pump();
    linux_apps_pump();
    wl_display_flush_clients(s.display);
    flush_motion(false);
    reap_child();
    lock_reap();
    return s.quit;
}

static void wlr_present(const uint16_t *px, int w, int h, bool changed)
{
    if (s.picture == NULL) return;
    s.frame = px;
    comp_route_set_frame(&s.route, px);
    bool relaid = false;
    if (w != s.route.fb_w || h != s.route.fb_h) {
        int ow = s.route.out_w, oh = s.route.out_h;
        relaid = comp_route_layout(&s.route, ow, oh, w, h);
    }
    if (changed) {
        /* The part of the frame to show came with it (L9e): the view
         * changes with the frame that needs it, not a frame later. */
        int v[4];
        native_frame_copied_view(v);
        if (comp_route_set_view(&s.route, v[0], v[1], v[2], v[3])) {
            relaid = true;
            s.stats.view_changes++;
        }
    }
    if (relaid) apply_layout();
    if (!changed && !s.recompose) return;
    compose();
    if (!changed) return;
    /* The backdrops take their colour from the frame, which may have come
     * after the place it was read for (a window moved over where a hole had
     * been): read again from each new frame. */
    window_t *bw;
    wl_list_for_each(bw, &s.windows, link) {
        const comp_window_t *cw = comp_route_window(&s.route, bw->id);
        if (cw != NULL && cw->placed && bw->backdrop != NULL) set_backdrop_colour(bw, cw);
    }
    recheck_pointer();
    uint64_t t = now_ms();
    if (s.stats.pictures > 0) {
        uint64_t gap = t - s.stats.last_picture_ms;
        s.stats.picture_gap_sum_ms += gap;
        if (gap > s.stats.picture_gap_max_ms) s.stats.picture_gap_max_ms = gap;
    }
    s.stats.last_picture_ms = t;
    s.stats.pictures++;
}

/* The windows a close is asked of and waited for: every xdg toplevel, and
 * the X11 windows that are on screen (not menus). */
static bool closable(const window_t *w)
{
    if (w->kind == WIN_XDG) return w->toplevel->base->initialized;
    return w->mapped && !is_or(w);
}

static int window_count(void)
{
    int n = 0;
    window_t *w;
    wl_list_for_each(w, &s.windows, link) n += w->kind == WIN_XDG || closable(w);
    return n;
}

/* Xwayland's own connection: not a program to wait for or signal (it ends
 * with wlr_xwayland_destroy, after its X11 programs). */
static bool is_xwayland(struct wl_client *c)
{
#if WLR_HAS_XWAYLAND
    return s.xwayland != NULL && s.xwayland->server != NULL && s.xwayland->server->client == c;
#else
    (void)c;
    return false;
#endif
}

static int client_count(void)
{
    int n = 0;
    struct wl_list *clients = wl_display_get_client_list(s.display);
    for (struct wl_list *it = clients->next; it != clients; it = it->next) {
        n += !is_xwayland(wl_client_from_link(it));
    }
    return n;
}

/* Signal every client process still connected (Xwayland among them), and
 * the X11 programs whose windows are still there. */
static int signal_clients(int sig)
{
    int n = 0;
#if WLR_HAS_XWAYLAND
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (w->kind == WIN_X11 && w->xs->pid > 0 && w->xs->pid != getpid() && kill(w->xs->pid, sig) == 0) n++;
    }
#endif
    struct wl_list *clients = wl_display_get_client_list(s.display);
    for (struct wl_list *it = clients->next; it != clients; it = it->next) {
        struct wl_client *c = wl_client_from_link(it);
        if (is_xwayland(c)) continue;
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
 * unsaved work and be answered -- until they have gone or LINGER_MS. The
 * control socket is still answered meanwhile (a shot shows the picture; a
 * quit ends the wait at once). */
static void linger(void)
{
    int asked = 0;
    window_t *w;
    wl_list_for_each(w, &s.windows, link) {
        if (closable(w)) {
            win_close(w);
            asked++;
        }
    }
    if (asked == 0) {
        /* The main loop's copy is gone: nothing may read it any more. */
        s.frame = NULL;
        comp_route_set_frame(&s.route, NULL);
        return;
    }
    fprintf(stderr, "native: compositor: asked %d window%s to close\n", asked, asked == 1 ? "" : "s");
    /* A copy of the frame of its own: the main loop's is gone by now. As
     * large as the frame can be (the base screen, which may be larger than
     * the high-resolution one since L9d), and taken at once, so the picture
     * never points at the main loop's copy. */
    int bw = 0, bh = 0;
    native_frame_base_size(&bw, &bh);
    int cap = bw * bh > 640 * 360 ? bw * bh : 640 * 360;
    uint16_t *frame = malloc((size_t)cap * sizeof(uint16_t));
    uint64_t t0 = now_ms();
    uint64_t closed_at = 0;
    uint32_t shown = 0;
    int fw0 = 0, fh0 = 0;
    if (frame != NULL && native_frame_copy(frame, cap, &fw0, &fh0, &shown)) {
        wlr_present(frame, fw0, fh0, true);
    } else {
        s.frame = NULL;
        comp_route_set_frame(&s.route, NULL);
    }
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
        linux_apps_pump();
        wl_display_flush_clients(s.display);
        reap_child();
        if (native_ctl_poll()) {
            fprintf(stderr, "native: compositor: quit asked for while waiting for the windows: not waiting\n");
            break;
        }
        if (frame != NULL && native_frame_seq() != shown) {
            int fw = 0, fh = 0;
            if (native_frame_copy(frame, cap, &fw, &fh, &shown)) wlr_present(frame, fw, fh, true);
        } else if (frame != NULL && s.frame == frame && s.recompose && s.route.fb_w > 0) {
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
/* The X11 programs that can still be told apart (their windows give a
 * pid), which end when signalled or when Xwayland goes. */
static int x11_programs(void)
{
    int n = 0;
#if WLR_HAS_XWAYLAND
    window_t *w;
    wl_list_for_each(w, &s.windows, link) n += w->kind == WIN_X11 && w->xs->pid > 0;
#endif
    return n;
}

static bool anyone_left(void)
{
    return client_count() > 0 || x11_programs() > 0;
}

static void end_clients(void)
{
    if (!anyone_left()) return;
    int n = signal_clients(SIGTERM);
    fprintf(stderr, "native: compositor: SIGTERM to %d client%s still connected\n", n, n == 1 ? "" : "s");
    uint64_t t0 = now_ms();
    while (anyone_left() && now_ms() - t0 < TERM_GRACE_MS) {
        wl_event_loop_dispatch(s.loop, 5);
        wl_display_flush_clients(s.display);
    }
    if (anyone_left()) {
        n = signal_clients(SIGKILL);
        fprintf(stderr, "native: compositor: SIGKILL to %d client%s\n", n, n == 1 ? "" : "s");
        t0 = now_ms();
        while (anyone_left() && now_ms() - t0 < TERM_GRACE_MS) {
            wl_event_loop_dispatch(s.loop, 5);
        }
    }
}

static void wlr_close(void)
{
    if (s.display == NULL) return;
    linux_logind_watch_stop();
    linger();
    wl_clip_fini();
    wl_ime_fini();
    end_clients();
    /* What the launcher started and is still there, with a window or not. */
    int left = linux_apps_end_all(TERM_GRACE_MS);
    if (left > 0) {
        fprintf(stderr, "native: compositor: ended %d process%s started from the launcher\n", left,
                left == 1 ? "" : "es");
    }
#if WLR_HAS_XWAYLAND
    if (s.xwayland != NULL) {
        /* wlroots wants its signals free of listeners by then. */
        wl_list_remove(&s.xw_new_surface.link);
        wl_list_remove(&s.xw_ready.link);
        wlr_xwayland_destroy(s.xwayland);
        s.xwayland = NULL;
    }
#endif
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
    .size    = wlr_size,
};
