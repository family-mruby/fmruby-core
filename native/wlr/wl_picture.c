/*
 * wl_picture.c - the compositor's picture: Family mruby's frame as a buffer,
 * and the Linux windows laid out under it (places, cuts, popups).
 * Split from output_wlr.c (L16), which has the overview; what the files
 * share is in output_wlr_internal.h.
 */

#include "output_wlr_internal.h"

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
void compose(void)
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
void set_backdrop_colour(window_t *w, const comp_window_t *cw)
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
void set_clip(window_t *w, struct wlr_scene_node *node, struct wlr_box box)
{
    if (box.x == w->clip.x && box.y == w->clip.y && box.width == w->clip.width && box.height == w->clip.height) {
        return;
    }
    w->clip = box;
    wlr_scene_subsurface_tree_set_clip(node, &box);
}

void apply_window(window_t *w)
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
void popup_clip(popup_t *p)
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
void close_popups(int keep)
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
void or_keep_inside(window_t *w)
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
void apply_layout(void)
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

void relayout(void)
{
    int ow = 0, oh = 0;
    if (s.output != NULL) wlr_output_effective_resolution(s.output, &ow, &oh);
    if (comp_route_layout(&s.route, ow, oh, s.route.fb_w, s.route.fb_h)) {
        apply_layout();
        lock_relayout();
    }
}

