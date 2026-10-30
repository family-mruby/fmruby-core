/*
 * wl_xwayland.c - the compositor's X11 windows (Xwayland, L9b).
 * Split from output_wlr.c (L16), which has the overview; what the files
 * share is in output_wlr_internal.h.
 */

#include "output_wlr_internal.h"

/* ------------------------------------------------------- X11 windows */

#if WLR_HAS_XWAYLAND
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
void xwayland_init(void)
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
void xwayland_init(void)
{
    unsetenv("DISPLAY");
    fprintf(stderr, "native: compositor: X11 programs cannot run here: this wlroots has no Xwayland\n");
}
#endif

