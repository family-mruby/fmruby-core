/*
 * output_wlr_internal.h - what the compositor's files share (L16): the
 * state, its types, and the functions one of them calls in another.
 * output_wlr.c holds the state and the "wlroots" output (native_backends.h,
 * the only way in for other parts); wl_window.c, wl_picture.c,
 * wl_xwayland.c, wl_input.c, wl_screen_lock.c and wl_tools.c the rest.
 * Not for other parts. Include it first (it defines _GNU_SOURCE).
 */

#ifndef OUTPUT_WLR_INTERNAL_H
#define OUTPUT_WLR_INTERNAL_H

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

struct output_wlr_state {
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
};

extern struct output_wlr_state s;

/* output_wlr.c */
uint64_t now_ms(void);
const char *renderer_name(void);
window_t *window_by_id(int id);
int toplevel_id(const struct wlr_xdg_toplevel *t);

/* wl_window.c */
bool is_or(const window_t *w);
struct wlr_surface *win_surface(const window_t *w);
struct wlr_box win_geometry(const window_t *w);
void win_close(window_t *w);
void window_name(const char *app_id, const char *alt, pid_t pid, char *out, size_t cap);
bool surface_origin(struct wlr_surface *surface, int *x, int *y);
void apply_focus(void);
void take_from_fmrb(void);
void begin_grab(window_t *w, grab_mode_t mode, uint32_t edges);
extern const float s_black[4];
void new_toplevel(struct wl_listener *l, void *data);
void new_popup(struct wl_listener *l, void *data);
void new_decoration(struct wl_listener *l, void *data);

/* wl_picture.c */
void compose(void);
void set_backdrop_colour(window_t *w, const comp_window_t *cw);
void set_clip(window_t *w, struct wlr_scene_node *node, struct wlr_box box);
void apply_window(window_t *w);
void popup_clip(popup_t *p);
void close_popups(int keep);
#if WLR_HAS_XWAYLAND
void or_keep_inside(window_t *w);
#endif
void apply_layout(void);
void relayout(void);

/* wl_xwayland.c */
void xwayland_init(void);

/* wl_input.c */
void recheck_pointer(void);
void follow_layout(void);
void add_keyboard(struct wlr_keyboard *wkb);
void add_ime_keyboard(struct wlr_keyboard *wkb);
void new_input(struct wl_listener *l, void *data);
void flush_motion(bool force);
void fmrb_cursor(bool show);
void process_motion(uint32_t time_msec);
void cursor_motion(struct wl_listener *l, void *data);
void cursor_motion_absolute(struct wl_listener *l, void *data);
void cursor_button(struct wl_listener *l, void *data);
void cursor_axis(struct wl_listener *l, void *data);
void cursor_frame(struct wl_listener *l, void *data);
void request_cursor(struct wl_listener *l, void *data);
void request_set_selection(struct wl_listener *l, void *data);

/* wl_screen_lock.c */
void lock_relayout(void);
void lock_start(const char *why);
void lock_key(keyboard_t *k, struct wlr_keyboard_key_event *ev);
void lock_motion(uint32_t time_msec);
void lock_reap(void);
void lock_init(const native_machine_t *m);

/* wl_tools.c */
void tool_devices_init(void);
bool wlr_ctl(const char *req, char *out, size_t cap);

#endif /* OUTPUT_WLR_INTERNAL_H */
