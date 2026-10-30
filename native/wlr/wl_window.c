/*
 * wl_window.c - the compositor's Linux windows: what xdg and X11 windows have
 * alike, the focus, the xdg toplevels, their popups and decorations.
 * Split from output_wlr.c (L16), which has the overview; what the files
 * share is in output_wlr_internal.h.
 */

#include "output_wlr_internal.h"

/* ---------------------------------------------- xdg and X11 windows alike */

bool is_or(const window_t *w)
{
#if WLR_HAS_XWAYLAND
    return w->kind == WIN_X11 && w->or_win;
#else
    (void)w;
    return false;
#endif
}

struct wlr_surface *win_surface(const window_t *w)
{
#if WLR_HAS_XWAYLAND
    if (w->kind == WIN_X11) return w->xs != NULL ? w->xs->surface : NULL;
#endif
    return w->toplevel->base->surface;
}

/* Where the window's own content starts in its surface (xdg: its geometry,
 * which may leave room for a shadow; X11: the surface's corner). */
struct wlr_box win_geometry(const window_t *w)
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
void win_close(window_t *w)
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
void window_name(const char *app_id, const char *alt, pid_t pid, char *out, size_t cap)
{
    if (linux_apps_available() && linux_apps_window_name(app_id, alt, pid, out, cap)) return;
    snprintf(out, cap, "%s", app_id && app_id[0] ? app_id : (alt && alt[0] ? alt : "linux"));
}

/* ---------------------------------------------------------- the focus */

/* Where a Linux window's surface has its origin on the output (the IME's
 * candidates go by its text cursor, L12b). */
bool surface_origin(struct wlr_surface *surface, int *x, int *y)
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

void apply_focus(void)
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

/* Family mruby's places, stacking and focus, and its requests. */
void take_from_fmrb(void)
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
void begin_grab(window_t *w, grab_mode_t mode, uint32_t edges)
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

const float s_black[4] = { 0, 0, 0, 1 };

void new_toplevel(struct wl_listener *l, void *data)
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

void new_popup(struct wl_listener *l, void *data)
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

void new_decoration(struct wl_listener *l, void *data)
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

