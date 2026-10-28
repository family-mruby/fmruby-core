/*
 * comp_route.c - see comp_route.h. Plain C, no wlroots, no allocation.
 */

#include "comp_route.h"

#include <stdio.h>
#include <string.h>

void comp_route_notice(comp_route_t *r, comp_notice_kind_t kind, int id, int x, int y, int w, int h,
                       const char *text)
{
    pthread_mutex_lock(&r->lock);
    if (r->notice_wr - r->notice_rd >= COMP_NOTICES) {
        r->notice_rd++;
        r->notices_dropped++;
    }
    comp_notice_t *n = &r->notice[r->notice_wr % COMP_NOTICES];
    n->kind = kind;
    n->id = id;
    n->x = x;
    n->y = y;
    n->w = w;
    n->h = h;
    snprintf(n->text, sizeof(n->text), "%s", text ? text : "");
    r->notice_wr++;
    pthread_mutex_unlock(&r->lock);
}

void comp_route_init(comp_route_t *r)
{
    memset(r, 0, sizeof(*r));
    pthread_mutex_init(&r->lock, NULL);
    r->focus = COMP_FMRB;
    r->place.scale = 1;
    r->show.scale = 1;
}

void comp_route_destroy(comp_route_t *r)
{
    pthread_mutex_destroy(&r->lock);
}

static bool base_frame(const comp_route_t *r)
{
    if (r->base_w <= 0 || r->base_h <= 0) return true;
    return r->fb_w == r->base_w && r->fb_h == r->base_h;
}

/* Where a window lies and whether it shows, from its place and the picture's. */
static void locate(const comp_route_t *r, comp_window_t *w)
{
    int s = r->place.scale > 0 ? r->place.scale : 1;
    w->visible = w->placed && w->shown && base_frame(r) && r->fb_w > 0 && !r->viewing;
    if (w->placed) {
        w->x = r->place.x + w->fx * s;
        w->y = r->place.y + w->fy * s;
    }
}

/* The view applies to the base frame only, inside it and smaller than it. */
static bool view_applies(const comp_route_t *r)
{
    if (r->view_w <= 0 || r->view_h <= 0 || r->fb_w <= 0 || r->fb_h <= 0) return false;
    if (r->base_w > 0 && !base_frame(r)) return false;
    if (r->view_x < 0 || r->view_y < 0 || r->view_x + r->view_w > r->fb_w || r->view_y + r->view_h > r->fb_h) {
        return false;
    }
    return r->view_w < r->fb_w || r->view_h < r->fb_h;
}

static void place_all(comp_route_t *r)
{
    if (r->out_w > 0 && r->out_h > 0 && r->fb_w > 0 && r->fb_h > 0) {
        fmrb_present_surface_t s = { .w = r->out_w, .h = r->out_h, .rotate = 0 };
        fmrb_present_place(&s, r->fb_w, r->fb_h, 0, &r->place);
        r->viewing = view_applies(r);
        if (r->viewing) fmrb_present_place(&s, r->view_w, r->view_h, 0, &r->show);
        else r->show = r->place;
    }
    for (int i = 0; i < r->n; i++) locate(r, &r->win[i]);
}

bool comp_route_layout(comp_route_t *r, int out_w, int out_h, int fb_w, int fb_h)
{
    if (out_w == r->out_w && out_h == r->out_h && fb_w == r->fb_w && fb_h == r->fb_h) return false;
    r->out_w = out_w;
    r->out_h = out_h;
    r->fb_w = fb_w;
    r->fb_h = fb_h;
    place_all(r);
    return true;
}

bool comp_route_set_view(comp_route_t *r, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) x = y = w = h = 0;
    if (x == r->view_x && y == r->view_y && w == r->view_w && h == r->view_h) return false;
    bool was = r->viewing;
    fmrb_present_place_t shown = r->show;
    r->view_x = x;
    r->view_y = y;
    r->view_w = w;
    r->view_h = h;
    place_all(r);
    return was != r->viewing || memcmp(&shown, &r->show, sizeof(shown)) != 0;
}

bool comp_route_to_frame(const comp_route_t *r, int x, int y, int *fx, int *fy)
{
    if (!r->viewing) return fmrb_present_to_frame(&r->place, r->fb_w, r->fb_h, x, y, fx, fy);
    bool in = fmrb_present_to_frame(&r->show, r->view_w, r->view_h, x, y, fx, fy);
    *fx += r->view_x;
    *fy += r->view_y;
    return in;
}

void comp_route_set_base(comp_route_t *r, int base_w, int base_h)
{
    r->base_w = base_w;
    r->base_h = base_h;
    place_all(r);
}

void comp_route_set_frame(comp_route_t *r, const uint16_t *px)
{
    r->px = px;
}

static int index_of(const comp_route_t *r, int id)
{
    for (int i = 0; i < r->n; i++) {
        if (r->win[i].id == id) return i;
    }
    return -1;
}

bool comp_route_window_add(comp_route_t *r, int id, int w, int h, int parent, const char *app_id)
{
    if (id <= 0 || r->n >= COMP_MAX_WINDOWS || index_of(r, id) >= 0) return false;
    comp_window_t *win = &r->win[r->n++];
    memset(win, 0, sizeof(*win));
    win->id = id;
    win->w = w;
    win->h = h;
    comp_route_notice(r, COMP_NOTICE_MAPPED, id, parent > 0 ? parent : 0, 0, w, h, app_id);
    return true;
}

bool comp_route_window_resize(comp_route_t *r, int id, int w, int h)
{
    int i = index_of(r, id);
    if (i < 0) return false;
    if (r->win[i].w == w && r->win[i].h == h) return true;
    r->win[i].w = w;
    r->win[i].h = h;
    comp_route_notice(r, COMP_NOTICE_RESIZED, id, 0, 0, w, h, NULL);
    return true;
}

bool comp_route_window_remove(comp_route_t *r, int id)
{
    int i = index_of(r, id);
    if (i < 0) return false;
    memmove(&r->win[i], &r->win[i + 1], sizeof(r->win[0]) * (size_t)(r->n - i - 1));
    r->n--;
    comp_route_notice(r, COMP_NOTICE_UNMAPPED, id, 0, 0, 0, 0, NULL);
    if (r->focus == id) {
        r->focus = COMP_FMRB;
        comp_route_notice(r, COMP_NOTICE_FOCUS, COMP_FMRB, 0, 0, 0, 0, NULL);
    }
    return true;
}

static bool listed_in(const comp_place_t *p, int n, int id)
{
    for (int j = 0; j < n; j++) {
        if (p[j].id == id) return true;
    }
    return false;
}

int comp_route_place(comp_route_t *r, const comp_place_t *p, int n, int focus)
{
    int changed = 0;
    /* Order: the windows Family mruby does not list stay at the bottom,
     * without a place (hidden), then the listed ones in the list's order. */
    comp_window_t sorted[COMP_MAX_WINDOWS];
    int k = 0;
    for (int i = 0; i < r->n; i++) {
        if (listed_in(p, n, r->win[i].id)) continue;
        comp_window_t w = r->win[i];
        if (w.placed) changed |= COMP_PLACE_MOVED;
        w.placed = false;
        locate(r, &w);
        sorted[k++] = w;
    }
    for (int j = 0; j < n; j++) {
        int i = index_of(r, p[j].id);
        if (i < 0) continue;
        bool dup = false;
        for (int m = 0; m < k; m++) {
            if (sorted[m].id == p[j].id) dup = true;
        }
        if (dup) continue;
        comp_window_t w = r->win[i];
        comp_window_t was = w;
        w.placed = true;
        w.shown = p[j].shown;
        w.fx = p[j].x;
        w.fy = p[j].y;
        w.fw = p[j].w;
        w.fh = p[j].h;
        w.state = p[j].state;
        locate(r, &w);
        if (!was.placed || was.visible != w.visible || was.fx != w.fx || was.fy != w.fy || was.fw != w.fw ||
            was.fh != w.fh || was.state != w.state) {
            changed |= COMP_PLACE_MOVED;
        }
        sorted[k++] = w;
    }
    for (int i = 0; i < k; i++) {
        if (r->win[i].id != sorted[i].id) changed |= COMP_PLACE_ORDER;
    }
    memcpy(r->win, sorted, sizeof(r->win[0]) * (size_t)k);
    r->n = k;
    int want = (focus != COMP_FMRB && index_of(r, focus) >= 0) ? focus : COMP_FMRB;
    if (want != r->focus) {
        r->focus = want;
        changed |= COMP_PLACE_FOCUS;
        comp_route_notice(r, COMP_NOTICE_FOCUS, want, 0, 0, 0, 0, NULL);
    }
    return changed;
}

const comp_window_t *comp_route_window(const comp_route_t *r, int id)
{
    int i = index_of(r, id);
    return i < 0 ? NULL : &r->win[i];
}

bool comp_route_wanted_size(const comp_route_t *r, int id, int *w, int *h)
{
    const comp_window_t *win = comp_route_window(r, id);
    if (win == NULL || !win->placed) return false;
    int s = r->place.scale > 0 ? r->place.scale : 1;
    *w = win->fw * s;
    *h = win->fh * s;
    return true;
}

static bool in_rect(const comp_window_t *w, int fx, int fy)
{
    return fx >= w->fx && fy >= w->fy && fx < w->fx + w->fw && fy < w->fy + w->fh;
}

bool comp_route_hole(const comp_route_t *r, int id, int fx, int fy)
{
    const comp_window_t *w = comp_route_window(r, id);
    if (w == NULL || !w->visible || r->px == NULL) return false;
    if (fx < 0 || fy < 0 || fx >= r->fb_w || fy >= r->fb_h || !in_rect(w, fx, fy)) return false;
    return r->px[(size_t)fy * (size_t)r->fb_w + (size_t)fx] == COMP_HOLE_565;
}

void comp_route_set_cursor(comp_route_t *r, bool on, int fx, int fy)
{
    r->cur_on = on;
    r->cur_fx = fx;
    r->cur_fy = fy;
}

static comp_hit_t window_hit(const comp_window_t *w, int x, int y, int fx, int fy)
{
    comp_hit_t hit = { .target = w->id };
    hit.x = x - w->x;
    hit.y = y - w->y;
    hit.fx = fx;
    hit.fy = fy;
    hit.in_picture = false;
    return hit;
}

comp_hit_t comp_route_at(const comp_route_t *r, int x, int y)
{
    comp_hit_t hit = { .target = COMP_FMRB };
    if (r->fb_w <= 0 || r->fb_h <= 0) return hit;
    int fx = 0, fy = 0;
    hit.in_picture = comp_route_to_frame(r, x, y, &fx, &fy);
    hit.x = fx;
    hit.y = fy;
    if (!hit.in_picture) return hit;
    for (int i = r->n - 1; i >= 0; i--) {
        const comp_window_t *w = &r->win[i];
        if (comp_route_hole(r, w->id, fx, fy)) return window_hit(w, x, y, fx, fy);
    }
    /* The arrow's tip: the pixel is the arrow's own, not the frame's. The
     * arrow reaches right and down from its tip, so the pixels just left of
     * it and just above it are the frame's. */
    if (r->cur_on && fx == r->cur_fx && fy == r->cur_fy) {
        static const int beside[][2] = { { -1, 0 }, { 0, -1 }, { -1, -1 } };
        for (int i = r->n - 1; i >= 0; i--) {
            const comp_window_t *w = &r->win[i];
            if (!w->visible || !in_rect(w, fx, fy)) continue;
            for (size_t k = 0; k < sizeof(beside) / sizeof(beside[0]); k++) {
                if (comp_route_hole(r, w->id, fx + beside[k][0], fy + beside[k][1])) {
                    return window_hit(w, x, y, fx, fy);
                }
            }
        }
    }
    return hit;
}

comp_hit_t comp_route_press(comp_route_t *r, int x, int y, bool *focus_changed)
{
    comp_hit_t hit = comp_route_at(r, x, y);
    bool changed = hit.target != r->focus;
    if (hit.target != COMP_FMRB) {
        /* Raise among the windows; Family mruby raises its stand-in too. */
        int i = index_of(r, hit.target);
        if (i >= 0 && i != r->n - 1) {
            comp_window_t w = r->win[i];
            memmove(&r->win[i], &r->win[i + 1], sizeof(r->win[0]) * (size_t)(r->n - i - 1));
            r->win[r->n - 1] = w;
        }
    }
    if (changed) {
        r->focus = hit.target;
        comp_route_notice(r, COMP_NOTICE_FOCUS, hit.target, 0, 0, 0, 0, NULL);
    }
    if (focus_changed != NULL) *focus_changed = changed;
    return hit;
}

int comp_route_focus(const comp_route_t *r)
{
    return r->focus;
}

void comp_route_compose(const comp_route_t *r, const uint16_t *px, uint32_t *out)
{
    size_t n = (size_t)r->fb_w * (size_t)r->fb_h;
    for (size_t i = 0; i < n; i++) out[i] = 0xFF000000u | fmrb_present_565_to_8888(px[i]);
    for (int i = 0; i < r->n; i++) {
        const comp_window_t *w = &r->win[i];
        if (!w->visible) continue;
        int x0 = w->fx < 0 ? 0 : w->fx;
        int y0 = w->fy < 0 ? 0 : w->fy;
        int x1 = w->fx + w->fw > r->fb_w ? r->fb_w : w->fx + w->fw;
        int y1 = w->fy + w->fh > r->fb_h ? r->fb_h : w->fy + w->fh;
        for (int fy = y0; fy < y1; fy++) {
            const uint16_t *row = px + (size_t)fy * (size_t)r->fb_w;
            uint32_t *orow = out + (size_t)fy * (size_t)r->fb_w;
            for (int fx = x0; fx < x1; fx++) {
                if (row[fx] == COMP_HOLE_565) orow[fx] = 0;
            }
        }
    }
}

bool comp_route_take_notice(comp_route_t *r, comp_notice_t *out)
{
    bool got = false;
    pthread_mutex_lock(&r->lock);
    if (r->notice_rd != r->notice_wr) {
        *out = r->notice[r->notice_rd % COMP_NOTICES];
        r->notice_rd++;
        got = true;
    }
    pthread_mutex_unlock(&r->lock);
    return got;
}

void comp_route_picture_box(const comp_route_t *r, int ox, int oy, int *x, int *y, int *w, int *h)
{
    *x = r->place.x - ox;
    *y = r->place.y - oy;
    *w = r->place.w;
    *h = r->place.h;
}

bool comp_route_inside(const comp_route_t *r, int *x, int *y, int w, int h)
{
    if (r->place.w <= 0 || r->place.h <= 0) return false;
    int nx = *x, ny = *y;
    if (nx + w > r->place.x + r->place.w) nx = r->place.x + r->place.w - w;
    if (ny + h > r->place.y + r->place.h) ny = r->place.y + r->place.h - h;
    if (nx < r->place.x) nx = r->place.x;
    if (ny < r->place.y) ny = r->place.y;
    bool moved = nx != *x || ny != *y;
    *x = nx;
    *y = ny;
    return moved;
}

bool comp_route_from_frame(const comp_route_t *r, int fx, int fy, int *x, int *y, int *scale)
{
    const fmrb_present_place_t *p = r->viewing ? &r->show : &r->place;
    int ox = r->viewing ? r->view_x : 0, oy = r->viewing ? r->view_y : 0;
    int vw = r->viewing ? r->view_w : r->fb_w, vh = r->viewing ? r->view_h : r->fb_h;
    int sc = p->scale > 0 ? p->scale : 1;
    *x = p->x + (fx - ox) * sc;
    *y = p->y + (fy - oy) * sc;
    *scale = sc;
    return fx >= ox && fy >= oy && fx < ox + vw && fy < oy + vh;
}

void comp_ime_place(int ax, int ay, int aw, int ah, int pw, int ph, int bx, int by, int bw, int bh, int *x, int *y)
{
    (void)aw;
    int nx = ax, ny = ay + ah;
    if (ny + ph > by + bh && ay - ph >= by) ny = ay - ph;
    if (ny + ph > by + bh) ny = by + bh - ph;
    if (nx + pw > bx + bw) nx = bx + bw - pw;
    if (nx < bx) nx = bx;
    if (ny < by) ny = by;
    *x = nx;
    *y = ny;
}

void comp_dialog_place(int px, int py, int pw, int ph, int ww, int wh, int fl, int ft, int fw, int fh,
                       int base_w, int base_h, int top, int *x, int *y)
{
    /* Middles in doubled units, so that odd sizes round the same way on
     * both sides. */
    int cx2 = 2 * px + pw;
    int cy2 = 2 * py + ph;
    int nx = (cx2 - (ww - fw)) / 2 - fl;
    int ny = (cy2 - (wh - fh)) / 2 - ft;
    if (nx + ww > base_w) nx = base_w - ww;
    if (ny + wh > base_h) ny = base_h - wh;
    if (nx < 0) nx = 0;
    if (ny < top) ny = top;
    *x = nx;
    *y = ny;
}
