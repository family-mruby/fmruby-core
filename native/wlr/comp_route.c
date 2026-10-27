/*
 * comp_route.c - see comp_route.h. Plain C, no wlroots, no allocation.
 */

#include "comp_route.h"

#include <string.h>

static void notice(comp_route_t *r, comp_notice_kind_t kind, int id, int w, int h)
{
    pthread_mutex_lock(&r->lock);
    if (r->notice_wr - r->notice_rd >= COMP_NOTICES) {
        r->notice_rd++;
        r->notices_dropped++;
    }
    comp_notice_t *n = &r->notice[r->notice_wr % COMP_NOTICES];
    n->kind = kind;
    n->id = id;
    n->w = w;
    n->h = h;
    r->notice_wr++;
    pthread_mutex_unlock(&r->lock);
}

void comp_route_init(comp_route_t *r)
{
    memset(r, 0, sizeof(*r));
    pthread_mutex_init(&r->lock, NULL);
    r->focus = COMP_FMRB;
    r->place.scale = 1;
}

void comp_route_destroy(comp_route_t *r)
{
    pthread_mutex_destroy(&r->lock);
}

static void centre(const comp_route_t *r, comp_window_t *w)
{
    w->x = (r->out_w - w->w) / 2;
    w->y = (r->out_h - w->h) / 2;
}

bool comp_route_layout(comp_route_t *r, int out_w, int out_h, int fb_w, int fb_h)
{
    if (out_w == r->out_w && out_h == r->out_h && fb_w == r->fb_w && fb_h == r->fb_h) return false;
    r->out_w = out_w;
    r->out_h = out_h;
    r->fb_w = fb_w;
    r->fb_h = fb_h;
    if (out_w > 0 && out_h > 0 && fb_w > 0 && fb_h > 0) {
        fmrb_present_surface_t s = { .w = out_w, .h = out_h, .rotate = 0 };
        fmrb_present_place(&s, fb_w, fb_h, 0, &r->place);
    }
    for (int i = 0; i < r->n; i++) centre(r, &r->win[i]);
    return true;
}

static int index_of(const comp_route_t *r, int id)
{
    for (int i = 0; i < r->n; i++) {
        if (r->win[i].id == id) return i;
    }
    return -1;
}

bool comp_route_window_add(comp_route_t *r, int id, int w, int h)
{
    if (id <= 0 || r->n >= COMP_MAX_WINDOWS || index_of(r, id) >= 0) return false;
    comp_window_t *win = &r->win[r->n++];
    win->id = id;
    win->w = w;
    win->h = h;
    centre(r, win);
    notice(r, COMP_NOTICE_MAPPED, id, w, h);
    return true;
}

bool comp_route_window_resize(comp_route_t *r, int id, int w, int h)
{
    int i = index_of(r, id);
    if (i < 0) return false;
    if (r->win[i].w == w && r->win[i].h == h) return true;
    r->win[i].w = w;
    r->win[i].h = h;
    centre(r, &r->win[i]);
    notice(r, COMP_NOTICE_RESIZED, id, w, h);
    return true;
}

bool comp_route_window_remove(comp_route_t *r, int id)
{
    int i = index_of(r, id);
    if (i < 0) return false;
    memmove(&r->win[i], &r->win[i + 1], sizeof(r->win[0]) * (size_t)(r->n - i - 1));
    r->n--;
    notice(r, COMP_NOTICE_UNMAPPED, id, 0, 0);
    if (r->focus == id) {
        r->focus = COMP_FMRB;
        notice(r, COMP_NOTICE_FOCUS, COMP_FMRB, 0, 0);
    }
    return true;
}

const comp_window_t *comp_route_window(const comp_route_t *r, int id)
{
    int i = index_of(r, id);
    return i < 0 ? NULL : &r->win[i];
}

comp_hit_t comp_route_at(const comp_route_t *r, int x, int y)
{
    comp_hit_t hit = { .target = COMP_FMRB };
    for (int i = r->n - 1; i >= 0; i--) {
        const comp_window_t *w = &r->win[i];
        if (x >= w->x && y >= w->y && x < w->x + w->w && y < w->y + w->h) {
            hit.target = w->id;
            hit.x = x - w->x;
            hit.y = y - w->y;
            return hit;
        }
    }
    if (r->fb_w > 0 && r->fb_h > 0) {
        hit.in_picture = fmrb_present_to_frame(&r->place, r->fb_w, r->fb_h, x, y, &hit.x, &hit.y);
    }
    return hit;
}

comp_hit_t comp_route_press(comp_route_t *r, int x, int y, bool *focus_changed)
{
    comp_hit_t hit = comp_route_at(r, x, y);
    bool changed = hit.target != r->focus;
    if (hit.target != COMP_FMRB) {
        /* Raise: move it to the top of the stack. */
        int i = index_of(r, hit.target);
        if (i >= 0 && i != r->n - 1) {
            comp_window_t w = r->win[i];
            memmove(&r->win[i], &r->win[i + 1], sizeof(r->win[0]) * (size_t)(r->n - i - 1));
            r->win[r->n - 1] = w;
        }
    }
    if (changed) {
        r->focus = hit.target;
        notice(r, COMP_NOTICE_FOCUS, hit.target, 0, 0);
    }
    if (focus_changed != NULL) *focus_changed = changed;
    return hit;
}

int comp_route_focus(const comp_route_t *r)
{
    return r->focus;
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
