/*
 * wl_touch.c - the compositor's touch screen (L17).
 * output_wlr.c has the overview; what the files share is in
 * output_wlr_internal.h.
 *
 * A touch acts where it touches. The device is mapped to the output, so
 * wlr_cursor turns its points with the output (display.rotate); the CRT
 * look's curve is undone as for the pointer (crt_point), and a touch that
 * lands outside the curved picture is dropped with its moves and release.
 *
 * Where it lands decides how it is delivered, as sway does
 * (cursor.c, handle_touch_down):
 *   - a Linux window's surface whose client takes wl_touch: wl_touch
 *     (down, motion, up, frame) to that surface, the motion in its
 *     coordinates wherever the finger goes; the press also focuses and
 *     raises the window as a click does (and Family mruby gets the press
 *     too, so its window manager raises the stand-in);
 *   - anything else (Family mruby's own picture, a client without
 *     wl_touch, the lock screen): pointer emulation -- the pointer goes to
 *     the point and the left button goes down, moves follow it, and it is
 *     released with the finger. One finger at a time; the others are
 *     ignored while it is down.
 * No gestures (long press, two-finger scroll): not in L17.
 */

#include "output_wlr_internal.h"

static void touch_dev_destroy(struct wl_listener *l, void *data)
{
    (void)data;
    touch_dev_t *t = wl_container_of(l, t, destroy);
    wl_list_remove(&t->destroy.link);
    wl_list_remove(&t->link);
    free(t);
    wlr_seat_set_capabilities(s.seat, seat_caps());
}

uint32_t seat_caps(void)
{
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD;
    if (!wl_list_empty(&s.touch_devs)) caps |= WL_SEAT_CAPABILITY_TOUCH;
    return caps;
}

void add_touch(struct wlr_input_device *dev)
{
    touch_dev_t *t = calloc(1, sizeof(*t));
    if (t == NULL) return;
    t->dev = dev;
    t->destroy.notify = touch_dev_destroy;
    wl_signal_add(&dev->events.destroy, &t->destroy);
    wl_list_insert(&s.touch_devs, &t->link);
    wlr_cursor_attach_input_device(s.cursor, dev);
    if (s.output != NULL) wlr_cursor_map_input_to_output(s.cursor, dev, s.output);
    fprintf(stderr, "native: compositor: touch screen %s%s\n", dev->name ? dev->name : "?",
            s.output != NULL ? ", mapped to the output" : "");
}

/* The output came: every touch screen is its (turned with it). */
void touch_map_output(void)
{
    touch_dev_t *t;
    wl_list_for_each(t, &s.touch_devs, link) {
        wlr_cursor_map_input_to_output(s.cursor, t->dev, s.output);
    }
}

/* ------------------------------------------------------------ points */

static touch_point_t *point_of(int32_t id)
{
    for (int i = 0; i < TOUCH_MAX; i++) {
        if (s.touch[i].mode != TOUCH_FREE && s.touch[i].id == id) return &s.touch[i];
    }
    return NULL;
}

static touch_point_t *point_new(int32_t id)
{
    for (int i = 0; i < TOUCH_MAX; i++) {
        if (s.touch[i].mode == TOUCH_FREE) {
            memset(&s.touch[i], 0, sizeof(s.touch[i]));
            s.touch[i].id = id;
            return &s.touch[i];
        }
    }
    return NULL;
}

static bool emulating(void)
{
    for (int i = 0; i < TOUCH_MAX; i++) {
        if (s.touch[i].mode == TOUCH_EMULATED) return true;
    }
    return false;
}

/* A touch device's point (0..1, turned by wlr_cursor) on the layout. */
static void to_layout(struct wlr_touch *touch, double x, double y, double *lx, double *ly)
{
    wlr_cursor_absolute_to_layout_coords(s.cursor, &touch->base, x, y, lx, ly);
}

/* The pointer's left button, for an emulated touch. */
static void emulate_button(uint32_t time_msec, bool down)
{
    struct wlr_pointer_button_event ev = {
        .pointer = NULL,
        .time_msec = time_msec,
        .button = BTN_LEFT,
        .state = down ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED,
    };
    cursor_button(NULL, &ev);
    wlr_seat_pointer_notify_frame(s.seat);
}

static void emulate_move(uint32_t time_msec, double lx, double ly)
{
    wlr_cursor_warp_closest(s.cursor, NULL, lx, ly);
    process_motion(time_msec);
}

static void touch_down(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_touch_down_event *ev = data;
    if (point_of(ev->touch_id) != NULL) return;
    touch_point_t *pt = point_new(ev->touch_id);
    if (pt == NULL) return;
    double lx, ly;
    to_layout(ev->touch, ev->x, ev->y, &lx, &ly);
    double px = lx, py = ly;
    bool inside = true;
    crt_point(&px, &py, &inside);
    if (!inside) {
        /* Where the curve leaves the picture nothing is shown: dropped. */
        pt->mode = TOUCH_IGNORED;
        s.stats.touch_ignored++;
        crt_dropped_press();
        return;
    }
    /* A drag of the mouse or a window being moved has the pointer. */
    if (s.grab.mode != GRAB_NONE || (s.buttons_held > 0 && !emulating())) {
        pt->mode = TOUCH_IGNORED;
        s.stats.touch_ignored++;
        return;
    }
    ptr_hit_t p = s.lk.locked ? (ptr_hit_t){ 0 } : point_hit(px, py);
    if (!s.lk.locked && p.hit.target != COMP_FMRB && p.surf != NULL && wlr_surface_accepts_touch(p.surf, s.seat)) {
        if (!p.popup) {
            comp_hit_t h = press_focus(px, py, ev->time_msec, BTN_LEFT);
            if (h.target != COMP_FMRB) {
                /* Family mruby's window manager raises the stand-in. */
                fmrb_press(h.fx, h.fy, 1, true);
                pt->fmrb_too = true;
                pt->fx = h.fx;
                pt->fy = h.fy;
                /* Its own arrow is not left drawn over the window. */
                fmrb_cursor(false);
                comp_route_set_cursor(&s.route, false, 0, 0);
            }
            /* The window may have moved in front: find the surface again. */
            p = point_hit(px, py);
            if (p.hit.target == COMP_FMRB || p.surf == NULL) {
                if (pt->fmrb_too) fmrb_press(pt->fx, pt->fy, 1, false);
                pt->mode = TOUCH_IGNORED;
                s.stats.touch_ignored++;
                return;
            }
        }
        pt->mode = TOUCH_NATIVE;
        pt->ox = px - p.sx;
        pt->oy = py - p.sy;
        wlr_seat_touch_notify_down(s.seat, p.surf, ev->time_msec, ev->touch_id, p.sx, p.sy);
        s.stats.touch_native++;
        return;
    }
    if (emulating()) {
        pt->mode = TOUCH_IGNORED;
        s.stats.touch_ignored++;
        return;
    }
    pt->mode = TOUCH_EMULATED;
    s.stats.touch_emulated++;
    emulate_move(ev->time_msec, lx, ly);
    emulate_button(ev->time_msec, true);
}

static void touch_motion(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_touch_motion_event *ev = data;
    touch_point_t *pt = point_of(ev->touch_id);
    if (pt == NULL) return;
    double lx, ly;
    to_layout(ev->touch, ev->x, ev->y, &lx, &ly);
    if (pt->mode == TOUCH_EMULATED) {
        emulate_move(ev->time_msec, lx, ly);
        return;
    }
    if (pt->mode != TOUCH_NATIVE) return;
    double px = lx, py = ly;
    crt_point(&px, &py, NULL);
    wlr_seat_touch_notify_motion(s.seat, ev->time_msec, ev->touch_id, px - pt->ox, py - pt->oy);
}

static void point_end(touch_point_t *pt, uint32_t time_msec)
{
    if (pt->mode == TOUCH_EMULATED) {
        emulate_button(time_msec, false);
    } else if (pt->mode == TOUCH_NATIVE) {
        wlr_seat_touch_notify_up(s.seat, time_msec, pt->id);
        if (pt->fmrb_too) fmrb_press(pt->fx, pt->fy, 1, false);
    }
    pt->mode = TOUCH_FREE;
}

static void touch_up(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_touch_up_event *ev = data;
    touch_point_t *pt = point_of(ev->touch_id);
    if (pt != NULL) point_end(pt, ev->time_msec);
}

static void touch_cancel(struct wl_listener *l, void *data)
{
    (void)l;
    struct wlr_touch_cancel_event *ev = data;
    touch_point_t *pt = point_of(ev->touch_id);
    if (pt == NULL) return;
    if (pt->mode == TOUCH_NATIVE) {
        struct wlr_touch_point *wp = wlr_seat_touch_get_point(s.seat, ev->touch_id);
        if (wp != NULL && wp->client != NULL) wlr_seat_touch_notify_cancel(s.seat, wp->client);
        if (pt->fmrb_too) fmrb_press(pt->fx, pt->fy, 1, false);
        pt->mode = TOUCH_FREE;
        return;
    }
    point_end(pt, ev->time_msec);
}

static void touch_frame(struct wl_listener *l, void *data)
{
    (void)l;
    (void)data;
    wlr_seat_touch_notify_frame(s.seat);
}

void touch_init(void)
{
    wl_list_init(&s.touch_devs);
    s.touch_down.notify = touch_down;
    wl_signal_add(&s.cursor->events.touch_down, &s.touch_down);
    s.touch_up.notify = touch_up;
    wl_signal_add(&s.cursor->events.touch_up, &s.touch_up);
    s.touch_motion.notify = touch_motion;
    wl_signal_add(&s.cursor->events.touch_motion, &s.touch_motion);
    s.touch_cancel.notify = touch_cancel;
    wl_signal_add(&s.cursor->events.touch_cancel, &s.touch_cancel);
    s.touch_frame.notify = touch_frame;
    wl_signal_add(&s.cursor->events.touch_frame, &s.touch_frame);
}

/* ------------------------------------------------- the tools' touch */

static const struct wlr_touch_impl s_tool_touch_impl = { .name = "fmrb-tools-touch" };

/* Made at the first "comp touch" (the plain one too, so that a check can
 * let the clients bind wl_touch before it touches), so that the seat does not say it has
 * touch (and the clients bind wl_touch) where there is no touch screen. */
static void tool_touch_make(void)
{
    if (s.tool_touch_made) return;
    wlr_touch_init(&s.tool_touch, &s_tool_touch_impl, "fmrb-tools-touch");
    s.tool_touch_made = true;
    add_touch(&s.tool_touch.base);
    wlr_seat_set_capabilities(s.seat, seat_caps());
}

/* An output pixel (as seen, after display.rotate) to the touch device's
 * own 0..1 (before it), the inverse of wlr_cursor's turn. */
static void tool_touch_norm(int x, int y, double *nx, double *ny)
{
    double u = s.route.out_w > 0 ? ((double)x + 0.5) / s.route.out_w : 0;
    double v = s.route.out_h > 0 ? ((double)y + 0.5) / s.route.out_h : 0;
    enum wl_output_transform tr = s.output != NULL ? s.output->transform : WL_OUTPUT_TRANSFORM_NORMAL;
    switch (tr) {
    case WL_OUTPUT_TRANSFORM_90: *nx = v; *ny = 1.0 - u; break;
    case WL_OUTPUT_TRANSFORM_180: *nx = 1.0 - u; *ny = 1.0 - v; break;
    case WL_OUTPUT_TRANSFORM_270: *nx = 1.0 - v; *ny = u; break;
    default: *nx = u; *ny = v; break;
    }
}

/*
 * The control socket's touch (wl_tools.c):
 *   comp touch                 "OK devices=N down=K native=.. emulated=.. ignored=.."
 *   comp touch down ID X Y     a finger down at output pixel (X, Y) as seen
 *   comp touch move ID X Y     ...moved there
 *   comp touch up ID           ...lifted
 *   comp touch tap X Y         down and up at once (id 0)
 * Each is followed by a frame. True when the request was a touch one.
 */
bool touch_tool(const char *a, char *out, size_t cap)
{
    if (strncmp(a, "touch", 5) != 0 || (a[5] != '\0' && a[5] != ' ')) return false;
    tool_touch_make();
    if (a[5] == '\0') {
        int devs = 0, down = 0;
        touch_dev_t *t;
        wl_list_for_each(t, &s.touch_devs, link) devs++;
        for (int i = 0; i < TOUCH_MAX; i++) down += s.touch[i].mode != TOUCH_FREE;
        snprintf(out, cap, "OK devices=%d down=%d native=%u emulated=%u ignored=%u\n", devs, down,
                 s.stats.touch_native, s.stats.touch_emulated, s.stats.touch_ignored);
        return true;
    }
    int id = 0, x = 0, y = 0;
    char verb[8] = "";
    int got = sscanf(a + 6, "%7s", verb);
    bool ok = got == 1;
    uint32_t t = (uint32_t)now_ms();
    double nx = 0, ny = 0;
    if (ok && strcmp(verb, "down") == 0 && sscanf(a + 6, "%*s %d %d %d", &id, &x, &y) == 3) {
        tool_touch_norm(x, y, &nx, &ny);
        struct wlr_touch_down_event ev = { .touch = &s.tool_touch, .time_msec = t, .touch_id = id, .x = nx, .y = ny };
        wl_signal_emit_mutable(&s.tool_touch.events.down, &ev);
    } else if (ok && strcmp(verb, "move") == 0 && sscanf(a + 6, "%*s %d %d %d", &id, &x, &y) == 3) {
        tool_touch_norm(x, y, &nx, &ny);
        struct wlr_touch_motion_event ev = { .touch = &s.tool_touch, .time_msec = t, .touch_id = id, .x = nx, .y = ny };
        wl_signal_emit_mutable(&s.tool_touch.events.motion, &ev);
    } else if (ok && strcmp(verb, "up") == 0 && sscanf(a + 6, "%*s %d", &id) == 1) {
        struct wlr_touch_up_event ev = { .touch = &s.tool_touch, .time_msec = t, .touch_id = id };
        wl_signal_emit_mutable(&s.tool_touch.events.up, &ev);
    } else if (ok && strcmp(verb, "tap") == 0 && sscanf(a + 6, "%*s %d %d", &x, &y) == 2) {
        tool_touch_norm(x, y, &nx, &ny);
        struct wlr_touch_down_event dn = { .touch = &s.tool_touch, .time_msec = t, .touch_id = 0, .x = nx, .y = ny };
        wl_signal_emit_mutable(&s.tool_touch.events.down, &dn);
        wl_signal_emit_mutable(&s.tool_touch.events.frame, NULL);
        struct wlr_touch_up_event up = { .touch = &s.tool_touch, .time_msec = t, .touch_id = 0 };
        wl_signal_emit_mutable(&s.tool_touch.events.up, &up);
    } else {
        snprintf(out, cap, "ERR comp touch [down ID X Y | move ID X Y | up ID | tap X Y]\n");
        return true;
    }
    wl_signal_emit_mutable(&s.tool_touch.events.frame, NULL);
    wl_display_flush_clients(s.display);
    snprintf(out, cap, "OK\n");
    return true;
}
