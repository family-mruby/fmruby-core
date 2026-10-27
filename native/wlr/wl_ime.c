/*
 * wl_ime.c - the PC's IME in the compositor (doc/linux_native, L12b).
 *
 * The compositor serves the Wayland IME protocols of wlroots 0.19:
 * input-method-v2 (the IME's side: fcitx5 and the like), text-input-v3 (the
 * applications' side) and virtual-keyboard-v1 (the IME hands back through it
 * the keys it does not use). One IME at a time; a second is told it is
 * unavailable.
 *
 * Linux windows: the relay every wlroots desktop has. The text input of the
 * focused window's client is entered; when it enables itself, the IME is
 * activated with its content type and surrounding text; what the IME
 * commits (the text being composed, the committed text, a deletion) is sent
 * on to it. The IME's candidate window (an input popup) lies over everything
 * at the window's text cursor, inside the picture's rectangle.
 *
 * Family mruby's apps: the compositor stands in as their text input. While
 * Family mruby has the keyboard and the focused app has said where its text
 * cursor is (FmrbApp.set_text_cursor, handed over by native_ime.h), the IME
 * is activated for it. Its committed text goes to the app as UTF-8 key
 * events (native_ime_commit), as Family mruby's own kana input sends a
 * composed kana. The text being composed is drawn here, in Family mruby's
 * font at the picture's scale, over the app at its cursor (the IME's own
 * popup shows candidates, not that text, for a client that shows it), and
 * the candidates go under it. An app that never said where its cursor is (a
 * game) gets its keys straight, without the IME.
 *
 * Keys: while the IME serves the focused text input and holds the keyboard
 * grab, the keyboards' keys go to it (output_wlr.c asks wl_ime_takes_keys),
 * the switching hotkeys excepted. The keys it does not use come back on its
 * virtual keyboard and go where the focus is, as any key. A key goes up
 * where it went down.
 *
 * The IME program (compositor.ime, "fcitx5"): started once the socket is
 * there, with WAYLAND_DISPLAY naming this compositor and without DISPLAY
 * (so it does not start Xwayland for its X11 frontend; X11 programs get no
 * IME from it). Not installed: one line, and Family mruby keeps its own kana
 * input. Whether an IME is there is told to the machine (native_ime.h) when
 * one binds input-method-v2 and when it goes.
 */

#define _GNU_SOURCE
#define WLR_USE_UNSTABLE 1

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>

#include "native_ime.h"
#include "wl_ime.h"

typedef struct ti {
    struct wl_list link;
    struct wlr_text_input_v3 *wlr;
    struct wl_listener enable, commit, disable, destroy;
} ti_t;

typedef struct ipop {
    struct wl_list link;
    struct wlr_input_popup_surface_v2 *wlr;
    struct wlr_scene_tree *tree;
    bool mapped;
    int x, y;                       /* where it was put */
    struct wlr_box sent;            /* the rectangle last told to it */
    struct wl_listener map, unmap, commit, destroy;
} ipop_t;

/* The text being composed, as a buffer of our own pixels. */
typedef struct {
    struct wlr_buffer base;
    uint32_t *px;
    int w, h;
} pre_buf_t;

#define PRE_CAP (1024 * 18)

static struct {
    struct wl_display *display;
    struct wlr_seat *seat;
    struct wlr_scene_tree *layer;
    const comp_route_t *route;
    wl_ime_host_t host;
    bool up;

    struct wlr_text_input_manager_v3 *ti_mgr;
    struct wlr_input_method_manager_v2 *im_mgr;
    struct wlr_virtual_keyboard_manager_v1 *vk_mgr;
    struct wl_listener new_ti, new_im, new_vk;

    struct wlr_input_method_v2 *im;
    struct wl_listener im_commit, im_destroy, im_new_popup, im_grab;
    struct wlr_input_method_keyboard_grab_v2 *grab;
    struct wl_listener grab_destroy;

    struct wl_list tis;             /* ti_t */
    struct wl_list popups;          /* ipop_t */

    struct wlr_surface *focus;      /* a Linux window's, or NULL */
    bool focus_fmrb;
    ti_t *active;                   /* the Linux text input the IME serves */
    bool fmrb_active;               /* the IME serves Family mruby */

    /* Family mruby's focused app's cursor (frame coordinates). */
    bool cur_on;
    int cur_fx, cur_fy, cur_h;
    unsigned cur_seq;

    /* The text being composed for Family mruby. */
    char preedit[512];
    int pre_begin, pre_end;
    struct wlr_scene_buffer *pre_node;
    int pre_x, pre_y, pre_w, pre_h;     /* on the output, 0x0 when not shown */
    uint32_t *pre_px;

    pid_t pid;
    char command[256];

    struct {
        unsigned activations;
        unsigned fmrb_commits, linux_commits;
        unsigned keys;                  /* keys sent to the IME */
        unsigned popups;                /* popups shown so far */
        unsigned preedits;              /* times Family mruby's composing text was drawn */
    } stats;
} m;

/* ------------------------------------------------------- the buffer */

static void pre_destroy(struct wlr_buffer *wb)
{
    pre_buf_t *b = wl_container_of(wb, b, base);
    wlr_buffer_finish(wb);
    free(b->px);
    free(b);
}

static bool pre_begin(struct wlr_buffer *wb, uint32_t flags, void **data, uint32_t *format, size_t *stride)
{
    pre_buf_t *b = wl_container_of(wb, b, base);
    if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) return false;
    *data = b->px;
    *format = DRM_FORMAT_ARGB8888;
    *stride = (size_t)b->w * 4;
    return true;
}

static void pre_end(struct wlr_buffer *wb)
{
    (void)wb;
}

static const struct wlr_buffer_impl s_pre_impl = {
    .destroy = pre_destroy,
    .begin_data_ptr_access = pre_begin,
    .end_data_ptr_access = pre_end,
};

/* ------------------------------------------------------ the places */

/* Family mruby's cursor on the output: (x, y) its top-left, (w, h) one cell
 * by the line. False when it is not shown. */
static bool fmrb_cursor_box(struct wlr_box *b)
{
    if (!m.cur_on || m.route == NULL) return false;
    int x = 0, y = 0, sc = 1;
    if (!comp_route_from_frame(m.route, m.cur_fx, m.cur_fy, &x, &y, &sc)) return false;
    b->x = x;
    b->y = y;
    b->width = sc * (m.cur_h > 0 ? m.cur_h / 2 : 4);
    b->height = sc * (m.cur_h > 0 ? m.cur_h : 8);
    return true;
}

/* The rectangle the candidates go by: the composing text (Family mruby), or
 * the text input's cursor rectangle on the output (a Linux window). */
static bool anchor_box(struct wlr_box *b)
{
    if (m.fmrb_active) {
        if (m.pre_w > 0) {
            *b = (struct wlr_box){ m.pre_x, m.pre_y, m.pre_w, m.pre_h };
            return true;
        }
        return fmrb_cursor_box(b);
    }
    if (m.active == NULL) return false;
    struct wlr_text_input_v3 *t = m.active->wlr;
    if (t->focused_surface == NULL || m.host.surface_origin == NULL) return false;
    int ox = 0, oy = 0;
    if (!m.host.surface_origin(t->focused_surface, &ox, &oy)) return false;
    struct wlr_box c = t->current.cursor_rectangle;
    if (!(t->active_features & WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE)) c = (struct wlr_box){ 0, 0, 1, 1 };
    *b = (struct wlr_box){ ox + c.x, oy + c.y, c.width > 0 ? c.width : 1, c.height > 0 ? c.height : 1 };
    return true;
}

static void popup_place(ipop_t *p)
{
    struct wlr_surface *surf = p->wlr->surface;
    bool on = p->mapped && (m.fmrb_active || m.active != NULL);
    struct wlr_box a;
    if (on && !anchor_box(&a)) on = false;
    wlr_scene_node_set_enabled(&p->tree->node, on);
    if (!on) return;
    const comp_route_t *r = m.route;
    int pw = surf->current.width, ph = surf->current.height;
    int x = 0, y = 0;
    comp_ime_place(a.x, a.y, a.width, a.height, pw, ph, r->place.x, r->place.y, r->place.w, r->place.h, &x, &y);
    wlr_scene_node_set_position(&p->tree->node, x, y);
    wlr_scene_node_raise_to_top(&p->tree->node);
    struct wlr_box clip = { r->place.x - x, r->place.y - y, r->place.w, r->place.h };
    wlr_scene_subsurface_tree_set_clip(&p->tree->node, &clip);
    struct wlr_box rel = { a.x - x, a.y - y, a.width, a.height };
    if (memcmp(&rel, &p->sent, sizeof(rel)) != 0) {
        p->sent = rel;
        wlr_input_popup_surface_v2_send_text_input_rectangle(p->wlr, &rel);
    }
    if (x != p->x || y != p->y) {
        p->x = x;
        p->y = y;
        fprintf(stderr, "native: compositor: IME candidates %dx%d at (%d,%d) for the text at (%d,%d %dx%d)\n", pw, ph,
                x, y, a.x, a.y, a.width, a.height);
    }
}

static void place_popups(void)
{
    ipop_t *p;
    wl_list_for_each(p, &m.popups, link) popup_place(p);
}

/* Family mruby's composing text, drawn again (or hidden). */
static void draw_preedit(void)
{
    if (m.pre_node == NULL) return;
    struct wlr_box c;
    if (!m.fmrb_active || m.preedit[0] == '\0' || !fmrb_cursor_box(&c)) {
        wlr_scene_node_set_enabled(&m.pre_node->node, false);
        m.pre_w = m.pre_h = 0;
        return;
    }
    if (m.pre_px == NULL) m.pre_px = malloc(PRE_CAP * sizeof(uint32_t));
    int w = 0, h = 0;
    if (m.pre_px == NULL ||
        !native_preedit_render(m.preedit, m.pre_begin, m.pre_end, m.cur_h, m.pre_px, PRE_CAP, &w, &h)) {
        wlr_scene_node_set_enabled(&m.pre_node->node, false);
        m.pre_w = m.pre_h = 0;
        return;
    }
    pre_buf_t *b = calloc(1, sizeof(*b));
    if (b == NULL) return;
    b->px = malloc((size_t)w * (size_t)h * 4);
    if (b->px == NULL) {
        free(b);
        return;
    }
    memcpy(b->px, m.pre_px, (size_t)w * (size_t)h * 4);
    b->w = w;
    b->h = h;
    wlr_buffer_init(&b->base, &s_pre_impl, w, h);
    wlr_scene_buffer_set_buffer(m.pre_node, &b->base);
    wlr_buffer_drop(&b->base);
    int sc = c.height / (m.cur_h > 0 ? m.cur_h : 8);
    if (sc < 1) sc = 1;
    /* Its text where the app's line is: a pixel of white above it and
     * left of it. Moved left to stay inside the picture. */
    int x = c.x - sc, y = c.y - sc;
    int ow = w * sc, oh = h * sc;
    const comp_route_t *r = m.route;
    if (x + ow > r->place.x + r->place.w) x = r->place.x + r->place.w - ow;
    if (x < r->place.x) x = r->place.x;
    if (y + oh > r->place.y + r->place.h) y = r->place.y + r->place.h - oh;
    if (y < r->place.y) y = r->place.y;
    wlr_scene_buffer_set_dest_size(m.pre_node, ow, oh);
    wlr_scene_node_set_position(&m.pre_node->node, x, y);
    wlr_scene_node_set_enabled(&m.pre_node->node, true);
    m.pre_x = x;
    m.pre_y = y;
    m.pre_w = ow;
    m.pre_h = oh;
    m.stats.preedits++;
}

/* ------------------------------------------------ the IME's state */

static void send_state(void)
{
    if (m.im == NULL) return;
    if (m.active != NULL) {
        struct wlr_text_input_v3 *t = m.active->wlr;
        if (t->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) {
            wlr_input_method_v2_send_surrounding_text(m.im, t->current.surrounding.text ? t->current.surrounding.text : "",
                                                      t->current.surrounding.cursor, t->current.surrounding.anchor);
        }
        wlr_input_method_v2_send_text_change_cause(m.im, t->current.text_change_cause);
        if (t->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE) {
            wlr_input_method_v2_send_content_type(m.im, t->current.content_type.hint, t->current.content_type.purpose);
        }
    } else {
        /* Family mruby's apps: plain text, nothing around it told. */
        wlr_input_method_v2_send_content_type(m.im, 0, 0);
    }
    wlr_input_method_v2_send_done(m.im);
}

/* The enabled text input of the focused window, if any. */
static ti_t *focused_ti(void)
{
    if (m.focus == NULL) return NULL;
    ti_t *t;
    wl_list_for_each(t, &m.tis, link) {
        if (t->wlr->focused_surface == m.focus && t->wlr->current_enabled) return t;
    }
    return NULL;
}

static void clear_preedit(void)
{
    m.preedit[0] = '\0';
    draw_preedit();
}

/* Who the IME serves now; activate or deactivate it on a change. */
static void update(void)
{
    ti_t *t = NULL;
    bool f = false;
    if (m.im != NULL) {
        if (m.focus_fmrb) {
            f = m.cur_on;
        } else {
            t = focused_ti();
        }
    }
    if (t == m.active && f == m.fmrb_active) {
        place_popups();
        return;
    }
    bool was = m.active != NULL || m.fmrb_active;
    if (m.active != NULL && m.active != t && m.active->wlr->focused_surface != NULL) {
        /* What it was composing goes with it. */
        wlr_text_input_v3_send_preedit_string(m.active->wlr, NULL, 0, 0);
        wlr_text_input_v3_send_done(m.active->wlr);
    }
    m.active = t;
    m.fmrb_active = f;
    if (!f) clear_preedit();
    if (m.im == NULL) return;
    if (was) {
        wlr_input_method_v2_send_deactivate(m.im);
        wlr_input_method_v2_send_done(m.im);
    }
    if (t != NULL || f) {
        wlr_input_method_v2_send_activate(m.im);
        send_state();
        m.stats.activations++;
        fprintf(stderr, "native: compositor: IME serves %s\n", f ? "Family mruby" : "a Linux window");
    }
    draw_preedit();
    place_popups();
}

/* ---------------------------------------------------- text inputs */

static void ti_enable(struct wl_listener *l, void *data)
{
    (void)data;
    (void)l;
    update();
}

static void ti_commit(struct wl_listener *l, void *data)
{
    (void)data;
    ti_t *t = wl_container_of(l, t, commit);
    if (t == m.active && m.im != NULL && t->wlr->current_enabled) {
        send_state();
        place_popups();
    }
}

static void ti_disable(struct wl_listener *l, void *data)
{
    (void)data;
    (void)l;
    update();
}

static void ti_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    ti_t *t = wl_container_of(l, t, destroy);
    wl_list_remove(&t->enable.link);
    wl_list_remove(&t->commit.link);
    wl_list_remove(&t->disable.link);
    wl_list_remove(&t->destroy.link);
    wl_list_remove(&t->link);
    if (m.active == t) {
        m.active = NULL;
        if (m.im != NULL) {
            wlr_input_method_v2_send_deactivate(m.im);
            wlr_input_method_v2_send_done(m.im);
        }
        update();
    }
    free(t);
}

static bool same_client(struct wl_resource *a, struct wlr_surface *s)
{
    return a != NULL && s != NULL && wl_resource_get_client(a) == wl_resource_get_client(s->resource);
}

static void new_ti(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_text_input_v3 *wt = data;
    ti_t *t = calloc(1, sizeof(*t));
    if (t == NULL) return;
    t->wlr = wt;
    t->enable.notify = ti_enable;
    wl_signal_add(&wt->events.enable, &t->enable);
    t->commit.notify = ti_commit;
    wl_signal_add(&wt->events.commit, &t->commit);
    t->disable.notify = ti_disable;
    wl_signal_add(&wt->events.disable, &t->disable);
    t->destroy.notify = ti_destroy;
    wl_signal_add(&wt->events.destroy, &t->destroy);
    wl_list_insert(&m.tis, &t->link);
    if (m.focus != NULL && same_client(wt->resource, m.focus)) wlr_text_input_v3_send_enter(wt, m.focus);
}

/* ------------------------------------------------------- popups */

static void pop_map(struct wl_listener *l, void *data)
{
    (void)data;
    ipop_t *p = wl_container_of(l, p, map);
    p->mapped = true;
    m.stats.popups++;
    popup_place(p);
}

static void pop_unmap(struct wl_listener *l, void *data)
{
    (void)data;
    ipop_t *p = wl_container_of(l, p, unmap);
    p->mapped = false;
    wlr_scene_node_set_enabled(&p->tree->node, false);
}

static void pop_commit(struct wl_listener *l, void *data)
{
    (void)data;
    ipop_t *p = wl_container_of(l, p, commit);
    if (p->mapped) popup_place(p);
}

static void pop_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    ipop_t *p = wl_container_of(l, p, destroy);
    wl_list_remove(&p->map.link);
    wl_list_remove(&p->unmap.link);
    wl_list_remove(&p->commit.link);
    wl_list_remove(&p->destroy.link);
    wl_list_remove(&p->link);
    wlr_scene_node_destroy(&p->tree->node);
    free(p);
}

static void im_new_popup(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_input_popup_surface_v2 *ps = data;
    ipop_t *p = calloc(1, sizeof(*p));
    if (p == NULL) return;
    p->wlr = ps;
    p->sent = (struct wlr_box){ -1, -1, -1, -1 };
    p->x = p->y = -100000;
    p->tree = wlr_scene_subsurface_tree_create(m.layer, ps->surface);
    if (p->tree == NULL) {
        free(p);
        return;
    }
    wlr_scene_node_set_enabled(&p->tree->node, false);
    p->map.notify = pop_map;
    wl_signal_add(&ps->surface->events.map, &p->map);
    p->unmap.notify = pop_unmap;
    wl_signal_add(&ps->surface->events.unmap, &p->unmap);
    p->commit.notify = pop_commit;
    wl_signal_add(&ps->surface->events.commit, &p->commit);
    p->destroy.notify = pop_destroy;
    wl_signal_add(&ps->events.destroy, &p->destroy);
    wl_list_insert(&m.popups, &p->link);
    if (ps->surface->mapped) pop_map(&p->map, NULL);
}

/* --------------------------------------------------------- the IME */

static void im_commit(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    struct wlr_input_method_v2 *im = m.im;
    if (im == NULL) return;
    const struct wlr_input_method_v2_state *st = &im->current;
    if (m.active != NULL) {
        struct wlr_text_input_v3 *t = m.active->wlr;
        if (st->preedit.text != NULL) {
            wlr_text_input_v3_send_preedit_string(t, st->preedit.text, st->preedit.cursor_begin,
                                                  st->preedit.cursor_end);
        }
        if (st->commit_text != NULL) {
            wlr_text_input_v3_send_commit_string(t, st->commit_text);
            m.stats.linux_commits++;
        }
        if (st->delete.before_length != 0 || st->delete.after_length != 0) {
            wlr_text_input_v3_send_delete_surrounding_text(t, st->delete.before_length, st->delete.after_length);
        }
        wlr_text_input_v3_send_done(t);
        return;
    }
    if (!m.fmrb_active) return;
    /* An omitted text being composed is an empty one (each commit is the
     * whole state). */
    snprintf(m.preedit, sizeof(m.preedit), "%s", st->preedit.text ? st->preedit.text : "");
    m.pre_begin = st->preedit.cursor_begin;
    m.pre_end = st->preedit.cursor_end;
    if (st->commit_text != NULL && st->commit_text[0] != '\0') {
        native_ime_commit(st->commit_text);
        m.stats.fmrb_commits++;
        fprintf(stderr, "native: compositor: IME committed %zu bytes to Family mruby\n", strlen(st->commit_text));
    }
    draw_preedit();
    place_popups();
}

static void grab_destroy(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    wl_list_remove(&m.grab_destroy.link);
    m.grab = NULL;
    /* The window gets the modifiers as they are now. */
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(m.seat);
    if (kb != NULL && m.focus != NULL) wlr_seat_keyboard_notify_modifiers(m.seat, &kb->modifiers);
}

static void im_grab(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_input_method_keyboard_grab_v2 *g = data;
    if (m.grab != NULL) wl_list_remove(&m.grab_destroy.link);
    m.grab = g;
    m.grab_destroy.notify = grab_destroy;
    wl_signal_add(&g->events.destroy, &m.grab_destroy);
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(m.seat);
    if (kb != NULL) wlr_input_method_keyboard_grab_v2_set_keyboard(g, kb);
}

static void im_destroy(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    wl_list_remove(&m.im_commit.link);
    wl_list_remove(&m.im_destroy.link);
    wl_list_remove(&m.im_new_popup.link);
    wl_list_remove(&m.im_grab.link);
    if (m.grab != NULL) {
        wl_list_remove(&m.grab_destroy.link);
        m.grab = NULL;
    }
    m.im = NULL;
    if (m.active != NULL && m.active->wlr->focused_surface != NULL) {
        wlr_text_input_v3_send_preedit_string(m.active->wlr, NULL, 0, 0);
        wlr_text_input_v3_send_done(m.active->wlr);
    }
    m.active = NULL;
    m.fmrb_active = false;
    clear_preedit();
    native_ime_set_present(false, "the IME");
}

static void new_im(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_input_method_v2 *im = data;
    if (m.im != NULL) {
        fprintf(stderr, "native: compositor: a second IME is unavailable (one at a time)\n");
        wlr_input_method_v2_send_unavailable(im);
        return;
    }
    m.im = im;
    m.im_commit.notify = im_commit;
    wl_signal_add(&im->events.commit, &m.im_commit);
    m.im_destroy.notify = im_destroy;
    wl_signal_add(&im->events.destroy, &m.im_destroy);
    m.im_new_popup.notify = im_new_popup;
    wl_signal_add(&im->events.new_popup_surface, &m.im_new_popup);
    m.im_grab.notify = im_grab;
    wl_signal_add(&im->events.grab_keyboard, &m.im_grab);
    pid_t pid = 0;
    wl_client_get_credentials(wl_resource_get_client(im->resource), &pid, NULL, NULL);
    char what[96];
    snprintf(what, sizeof(what), "an IME (input-method-v2, pid %d)", (int)pid);
    native_ime_set_present(true, what);
    update();
}

static void new_vk(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_virtual_keyboard_v1 *vk = data;
    if (m.host.add_keyboard != NULL) m.host.add_keyboard(&vk->keyboard);
}

/* ---------------------------------------------------------- keys */

bool wl_ime_own_keyboard(struct wlr_keyboard *kb)
{
    struct wlr_virtual_keyboard_v1 *vk = wlr_input_device_get_virtual_keyboard(&kb->base);
    if (vk == NULL || m.grab == NULL) return false;
    return wl_resource_get_client(vk->resource) == wl_resource_get_client(m.grab->resource);
}

bool wl_ime_takes_keys(struct wlr_keyboard *kb)
{
    if (!m.up || m.grab == NULL || m.im == NULL) return false;
    if (m.active == NULL && !m.fmrb_active) return false;
    return !wl_ime_own_keyboard(kb);
}

bool wl_ime_key(struct wlr_keyboard *kb, uint32_t time_msec, uint32_t code, uint32_t state)
{
    if (m.grab == NULL) return false;
    wlr_input_method_keyboard_grab_v2_set_keyboard(m.grab, kb);
    wlr_input_method_keyboard_grab_v2_send_key(m.grab, time_msec, code, state);
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) m.stats.keys++;
    return true;
}

bool wl_ime_modifiers(struct wlr_keyboard *kb)
{
    if (!wl_ime_takes_keys(kb)) return false;
    wlr_input_method_keyboard_grab_v2_set_keyboard(m.grab, kb);
    wlr_input_method_keyboard_grab_v2_send_modifiers(m.grab, &kb->modifiers);
    return true;
}

/* ---------------------------------------------------------- focus */

void wl_ime_focus(struct wlr_surface *surface, bool fmrb)
{
    if (!m.up) return;
    if (surface != m.focus) {
        ti_t *t;
        wl_list_for_each(t, &m.tis, link) {
            if (t->wlr->focused_surface != NULL && t->wlr->focused_surface != surface) {
                wlr_text_input_v3_send_leave(t->wlr);
            }
        }
        m.focus = surface;
        wl_list_for_each(t, &m.tis, link) {
            if (surface != NULL && t->wlr->focused_surface != surface && same_client(t->wlr->resource, surface)) {
                wlr_text_input_v3_send_enter(t->wlr, surface);
            }
        }
    }
    m.focus_fmrb = fmrb;
    update();
}

/* ---------------------------------------------------- the program */

static bool in_path(const char *prog)
{
    if (strchr(prog, '/') != NULL) return access(prog, X_OK) == 0;
    const char *path = getenv("PATH");
    if (path == NULL) path = "/usr/local/bin:/usr/bin:/bin";
    char dir[512];
    while (*path) {
        size_t n = strcspn(path, ":");
        if (n > 0 && n < sizeof(dir) - 64) {
            char full[1024];
            snprintf(full, sizeof(full), "%.*s/%s", (int)n, path, prog);
            if (access(full, X_OK) == 0) return true;
        }
        path += n;
        if (*path == ':') path++;
    }
    (void)dir;
    return false;
}

void wl_ime_start(const char *command, const char *socket)
{
    if (!m.up || command == NULL || command[0] == '\0') {
        fprintf(stderr, "native: compositor: no IME program set (compositor.ime): Family mruby keeps its own kana "
                        "input unless an IME connects\n");
        return;
    }
    char prog[128];
    if (sscanf(command, "%127s", prog) != 1) return;
    if (!in_path(prog)) {
        fprintf(stderr, "native: compositor: IME: %s is not installed: Family mruby keeps its own kana input\n", prog);
        return;
    }
    snprintf(m.command, sizeof(m.command), "%s", command);
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "native: compositor: IME: fork: %s\n", strerror(errno));
        return;
    }
    if (pid == 0) {
        setsid();
        /* Not on Xwayland: it is started for the first X11 program only. */
        unsetenv("DISPLAY");
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    m.pid = pid;
    fprintf(stderr, "native: compositor: IME: started \"%s\" (pid %d) on %s\n", command, (int)pid, socket);
}

static void reap(void)
{
    if (m.pid <= 0) return;
    int st = 0;
    pid_t r = waitpid(m.pid, &st, WNOHANG);
    if (r == m.pid || (r < 0 && errno == ECHILD)) {
        fprintf(stderr, "native: compositor: IME: \"%s\" ended (%d)\n", m.command,
                r == m.pid ? (WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st)) : -1);
        m.pid = 0;
    }
}

/* ------------------------------------------------------- the rest */

void wl_ime_init(struct wl_display *display, struct wlr_seat *seat, struct wlr_scene_tree *layer,
                 const comp_route_t *route, const wl_ime_host_t *host)
{
    memset(&m, 0, sizeof(m));
    m.display = display;
    m.seat = seat;
    m.layer = layer;
    m.route = route;
    if (host != NULL) m.host = *host;
    wl_list_init(&m.tis);
    wl_list_init(&m.popups);
    m.focus_fmrb = true;            /* the route starts with Family mruby's */
    m.ti_mgr = wlr_text_input_manager_v3_create(display);
    m.im_mgr = wlr_input_method_manager_v2_create(display);
    m.vk_mgr = wlr_virtual_keyboard_manager_v1_create(display);
    m.new_ti.notify = new_ti;
    wl_signal_add(&m.ti_mgr->events.text_input, &m.new_ti);
    m.new_im.notify = new_im;
    wl_signal_add(&m.im_mgr->events.input_method, &m.new_im);
    m.new_vk.notify = new_vk;
    wl_signal_add(&m.vk_mgr->events.new_virtual_keyboard, &m.new_vk);
    m.pre_node = wlr_scene_buffer_create(layer, NULL);
    wlr_scene_buffer_set_filter_mode(m.pre_node, WLR_SCALE_FILTER_NEAREST);
    wlr_scene_node_set_enabled(&m.pre_node->node, false);
    m.up = true;
}

void wl_ime_pump(void)
{
    if (!m.up) return;
    reap();
    int fx = 0, fy = 0, h = 0;
    unsigned seq = 0;
    bool on = native_ime_cursor(&fx, &fy, &h, &seq);
    if (seq != m.cur_seq) {
        m.cur_seq = seq;
        bool was = m.cur_on;
        m.cur_on = on;
        m.cur_fx = fx;
        m.cur_fy = fy;
        m.cur_h = h;
        if (was != on) {
            update();
        } else {
            draw_preedit();
            place_popups();
        }
    }
}

void wl_ime_relayout(void)
{
    if (!m.up) return;
    draw_preedit();
    place_popups();
}

void wl_ime_fini(void)
{
    if (!m.up) return;
    if (m.pid > 0) {
        kill(-m.pid, SIGTERM);
        for (int i = 0; i < 50 && waitpid(m.pid, NULL, WNOHANG) == 0; i++) usleep(10 * 1000);
        m.pid = 0;
    }
    free(m.pre_px);
    m.pre_px = NULL;
    m.up = false;
}

void wl_ime_describe(char *out, size_t cap)
{
    int n_pop = 0, shown = 0;
    char where[160] = "-";
    ipop_t *p;
    wl_list_for_each(p, &m.popups, link) {
        n_pop++;
        if (p->mapped && p->tree->node.enabled) {
            shown++;
            snprintf(where, sizeof(where), "%dx%d+%d+%d", p->wlr->surface->current.width,
                     p->wlr->surface->current.height, p->x, p->y);
        }
    }
    struct wlr_box c = { 0, 0, 0, 0 };
    bool has_cur = fmrb_cursor_box(&c);
    snprintf(out, cap,
             "OK present=%d grab=%d serves=%s cursor=%s%d,%d,%d cursor_out=%d,%d,%dx%d preedit_bytes=%zu preedit=%dx%d+%d+%d "
             "popups=%d shown=%d popup=%s activations=%u fmrb_commits=%u linux_commits=%u keys=%u popups_seen=%u "
             "preedits=%u pid=%d\n",
             m.im != NULL, m.grab != NULL, m.fmrb_active ? "fmrb" : m.active != NULL ? "linux" : "none",
             m.cur_on ? "" : "off:", m.cur_fx, m.cur_fy, m.cur_h, has_cur ? c.x : -1, has_cur ? c.y : -1, c.width,
             c.height, strlen(m.preedit), m.pre_w, m.pre_h, m.pre_x, m.pre_y, n_pop, shown, where,
             m.stats.activations, m.stats.fmrb_commits, m.stats.linux_commits, m.stats.keys, m.stats.popups,
             m.stats.preedits, (int)m.pid);
}
