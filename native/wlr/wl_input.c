/*
 * wl_input.c - the compositor's keyboard and pointer.
 * Split from output_wlr.c (L16), which has the overview; what the files
 * share is in output_wlr_internal.h.
 */

#include "output_wlr_internal.h"

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

void follow_layout(void)
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

void add_keyboard(struct wlr_keyboard *wkb)
{
    add_keyboard_as(wkb, false);
}

/* The IME's virtual keyboard (L12b): the keys it hands back, in its keymap. */
void add_ime_keyboard(struct wlr_keyboard *wkb)
{
    add_keyboard_as(wkb, true);
}

void new_input(struct wl_listener *l, void *data)
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

void flush_motion(bool force)
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
void fmrb_cursor(bool show)
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

void process_motion(uint32_t time_msec)
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
void recheck_pointer(void)
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

void cursor_motion(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_pointer_motion_event *ev = data;
    wlr_cursor_move(s.cursor, &ev->pointer->base, ev->delta_x, ev->delta_y);
    process_motion(ev->time_msec);
}

void cursor_motion_absolute(struct wl_listener *l, void *data)
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

void cursor_button(struct wl_listener *l, void *data)
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

void cursor_axis(struct wl_listener *l, void *data)
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

void cursor_frame(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    wlr_seat_pointer_notify_frame(s.seat);
}

void request_cursor(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_seat_pointer_request_set_cursor_event *ev = data;
    if (s.seat->pointer_state.focused_client == ev->seat_client) {
        wlr_cursor_set_surface(s.cursor, ev->surface, ev->hotspot_x, ev->hotspot_y);
    }
}

void request_set_selection(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_seat_request_set_selection_event *ev = data;
    wlr_seat_set_selection(s.seat, ev->source, ev->serial);
}

