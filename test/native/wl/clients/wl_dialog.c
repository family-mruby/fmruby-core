/*
 * wl_dialog.c - a Wayland client for the compositor's checks
 * (test/native/wl/wl_dialog_check.rb, doc/linux_native/ L9c): a window with
 * a dialog (an xdg toplevel whose parent is set) and menus (xdg popups),
 * driven by lines on a FIFO, telling what it gets on a log.
 *
 *   wl_dialog FIFO LOG
 *
 * Starts one toplevel, "Parent" (app id fmrb-parent, 300x150, green). The
 * lines it takes:
 *
 *   dialog W H          a toplevel "Dialog" (app id fmrb-dialog) with the
 *                       parent set, W x H, blue, with a red button of 40x24
 *                       in its bottom-right corner: a press and release on
 *                       the button closes the dialog
 *   popup X Y W H [A]   a popup of the parent, W x H, yellow, anchored to the
 *                       point (X, Y) of the parent (the anchor rectangle
 *                       1x1, gravity bottom-right), with the constraint
 *                       adjustments A ("slide": slide x and y, flip x and y;
 *                       "none": none; default slide); a grab with the last
 *                       button press's serial when there is one
 *   unpopup             the popup destroyed
 *   quit                end
 *
 * The log, one line each: "configure parent|dialog W H", "popup configure
 * X Y W H" (its place relative to the parent), "popup done", "key
 * parent|dialog CODE 1|0", "enter parent|dialog|popup X Y" (pointer),
 * "button parent|dialog|popup B 1|0 X Y", "kbd parent|dialog|popup"
 * (keyboard focus), "dialog closed by its button", "close parent|dialog"
 * (the compositor asked).
 *
 * A test program, not part of the product: plain libc and libwayland.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#define BTN_W 40
#define BTN_H 24

typedef struct {
    const char *name;
    struct wl_surface *surface;
    struct xdg_surface *xdg;
    struct xdg_toplevel *top;
    struct xdg_popup *popup;
    struct wl_buffer *buffer;
    int w, h;
    uint32_t colour;
    bool configured;
} surf_t;

static struct {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    surf_t parent, dialog, popup;
    surf_t *ptr_on;
    double px, py;
    uint32_t last_press_serial;
    bool btn_down_on_button;
    FILE *log;
    bool quit;
} c;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(c.log, fmt, ap);
    va_end(ap);
    fputc('\n', c.log);
    fflush(c.log);
}

static surf_t *by_surface(struct wl_surface *s)
{
    if (s == NULL) return NULL;
    if (c.parent.surface == s) return &c.parent;
    if (c.dialog.surface == s) return &c.dialog;
    if (c.popup.surface == s) return &c.popup;
    return NULL;
}

/* ------------------------------------------------------------ drawing */

static void draw(surf_t *s)
{
    int w = s->w, h = s->h;
    int stride = w * 4;
    size_t size = (size_t)stride * (size_t)h;
    int fd = memfd_create("wl_dialog", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size) != 0) {
        say("error: no buffer (%s)", strerror(errno));
        if (fd >= 0) close(fd);
        return;
    }
    uint32_t *px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (px == MAP_FAILED) {
        close(fd);
        return;
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t col = s->colour;
            if (s == &c.dialog && x >= w - BTN_W && y >= h - BTN_H) col = 0xFFFF0000u;
            px[y * w + x] = col;
        }
    }
    munmap(px, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, (int32_t)size);
    if (s->buffer != NULL) wl_buffer_destroy(s->buffer);
    s->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_surface_attach(s->surface, s->buffer, 0, 0);
    wl_surface_damage_buffer(s->surface, 0, 0, w, h);
    wl_surface_commit(s->surface);
}

/* --------------------------------------------------------- xdg-shell */

static void wm_ping(void *data, struct xdg_wm_base *wm, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm, serial);
}

static const struct xdg_wm_base_listener wm_listener = { .ping = wm_ping };

static void xdg_configure(void *data, struct xdg_surface *xdg, uint32_t serial)
{
    surf_t *s = data;
    xdg_surface_ack_configure(xdg, serial);
    s->configured = true;
    draw(s);
}

static const struct xdg_surface_listener xdg_listener = { .configure = xdg_configure };

static void top_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *states)
{
    (void)t;
    (void)states;
    surf_t *s = data;
    if (w > 0 && h > 0) {
        s->w = w;
        s->h = h;
    }
    say("configure %s %d %d", s->name, s->w, s->h);
}

static void destroy_surf(surf_t *s)
{
    if (s->popup) xdg_popup_destroy(s->popup);
    if (s->top) xdg_toplevel_destroy(s->top);
    if (s->xdg) xdg_surface_destroy(s->xdg);
    if (s->surface) wl_surface_destroy(s->surface);
    if (s->buffer) wl_buffer_destroy(s->buffer);
    if (c.ptr_on == s) c.ptr_on = NULL;
    const char *name = s->name;
    memset(s, 0, sizeof(*s));
    s->name = name;
}

static void top_close(void *data, struct xdg_toplevel *t)
{
    (void)t;
    surf_t *s = data;
    say("close %s", s->name);
    if (s == &c.parent) {
        c.quit = true;
    } else {
        destroy_surf(s);
    }
}

static void top_bounds(void *data, struct xdg_toplevel *t, int32_t w, int32_t h)
{
    (void)data;
    (void)t;
    (void)w;
    (void)h;
}

static void top_caps(void *data, struct xdg_toplevel *t, struct wl_array *caps)
{
    (void)data;
    (void)t;
    (void)caps;
}

static const struct xdg_toplevel_listener top_listener = {
    .configure = top_configure,
    .close = top_close,
    .configure_bounds = top_bounds,
    .wm_capabilities = top_caps,
};

static void popup_configure(void *data, struct xdg_popup *p, int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)p;
    surf_t *s = data;
    s->w = w;
    s->h = h;
    say("popup configure %d %d %d %d", x, y, w, h);
}

static void popup_done(void *data, struct xdg_popup *p)
{
    (void)p;
    say("popup done");
    destroy_surf(data);
}

static void popup_repositioned(void *data, struct xdg_popup *p, uint32_t token)
{
    (void)data;
    (void)p;
    (void)token;
}

static const struct xdg_popup_listener popup_listener = {
    .configure = popup_configure,
    .popup_done = popup_done,
    .repositioned = popup_repositioned,
};

static void make_toplevel(surf_t *s, const char *title, const char *app_id, int w, int h, uint32_t colour,
                          struct xdg_toplevel *parent)
{
    s->w = w;
    s->h = h;
    s->colour = colour;
    s->surface = wl_compositor_create_surface(c.compositor);
    s->xdg = xdg_wm_base_get_xdg_surface(c.wm, s->surface);
    xdg_surface_add_listener(s->xdg, &xdg_listener, s);
    s->top = xdg_surface_get_toplevel(s->xdg);
    xdg_toplevel_add_listener(s->top, &top_listener, s);
    xdg_toplevel_set_title(s->top, title);
    xdg_toplevel_set_app_id(s->top, app_id);
    if (parent != NULL) xdg_toplevel_set_parent(s->top, parent);
    wl_surface_commit(s->surface);
}

static void make_popup(int x, int y, int w, int h, bool slide)
{
    if (c.popup.surface != NULL) destroy_surf(&c.popup);
    surf_t *s = &c.popup;
    s->w = w;
    s->h = h;
    s->colour = 0xFFFFFF00u;
    s->surface = wl_compositor_create_surface(c.compositor);
    s->xdg = xdg_wm_base_get_xdg_surface(c.wm, s->surface);
    xdg_surface_add_listener(s->xdg, &xdg_listener, s);
    struct xdg_positioner *pos = xdg_wm_base_create_positioner(c.wm);
    xdg_positioner_set_size(pos, w, h);
    xdg_positioner_set_anchor_rect(pos, x, y, 1, 1);
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_TOP_LEFT);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    xdg_positioner_set_constraint_adjustment(
        pos, slide ? (XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y |
                      XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y)
                   : XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_NONE);
    s->popup = xdg_surface_get_popup(s->xdg, c.parent.xdg, pos);
    xdg_positioner_destroy(pos);
    xdg_popup_add_listener(s->popup, &popup_listener, s);
    if (c.last_press_serial != 0) xdg_popup_grab(s->popup, c.seat, c.last_press_serial);
    wl_surface_commit(s->surface);
}

/* -------------------------------------------------------------- input */

static void ptr_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surf, wl_fixed_t x,
                      wl_fixed_t y)
{
    (void)data;
    (void)p;
    (void)serial;
    c.ptr_on = by_surface(surf);
    c.px = wl_fixed_to_double(x);
    c.py = wl_fixed_to_double(y);
    say("enter %s %.1f %.1f", c.ptr_on ? c.ptr_on->name : "?", c.px, c.py);
}

static void ptr_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surf)
{
    (void)data;
    (void)p;
    (void)serial;
    (void)surf;
    c.ptr_on = NULL;
}

static void ptr_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    (void)data;
    (void)p;
    (void)time;
    c.px = wl_fixed_to_double(x);
    c.py = wl_fixed_to_double(y);
}

static void ptr_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time, uint32_t button,
                       uint32_t state)
{
    (void)data;
    (void)p;
    (void)time;
    surf_t *s = c.ptr_on;
    bool down = state == WL_POINTER_BUTTON_STATE_PRESSED;
    if (down) c.last_press_serial = serial;
    say("button %s %u %d %.1f %.1f", s ? s->name : "?", button, down ? 1 : 0, c.px, c.py);
    bool on_button = s == &c.dialog && c.px >= s->w - BTN_W && c.py >= s->h - BTN_H;
    if (down) {
        c.btn_down_on_button = on_button;
    } else if (on_button && c.btn_down_on_button) {
        c.btn_down_on_button = false;
        say("dialog closed by its button");
        destroy_surf(&c.dialog);
    }
}

static void ptr_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t v)
{
    (void)data;
    (void)p;
    (void)time;
    (void)axis;
    (void)v;
}

static const struct wl_pointer_listener ptr_listener = {
    .enter = ptr_enter,
    .leave = ptr_leave,
    .motion = ptr_motion,
    .button = ptr_button,
    .axis = ptr_axis,
};

static void kb_keymap(void *data, struct wl_keyboard *k, uint32_t format, int32_t fd, uint32_t size)
{
    (void)data;
    (void)k;
    (void)format;
    (void)size;
    close(fd);
}

static surf_t *s_kbd_on;

static void kb_enter(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *surf,
                     struct wl_array *keys)
{
    (void)data;
    (void)k;
    (void)serial;
    (void)keys;
    s_kbd_on = by_surface(surf);
    say("kbd %s", s_kbd_on ? s_kbd_on->name : "?");
}

static void kb_leave(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *surf)
{
    (void)data;
    (void)k;
    (void)serial;
    (void)surf;
    s_kbd_on = NULL;
}

static void kb_key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    (void)data;
    (void)k;
    (void)serial;
    (void)time;
    say("key %s %u %u", s_kbd_on ? s_kbd_on->name : "?", key, state);
}

static void kb_mods(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t d, uint32_t l, uint32_t lk,
                    uint32_t g)
{
    (void)data;
    (void)k;
    (void)serial;
    (void)d;
    (void)l;
    (void)lk;
    (void)g;
}

static void kb_repeat(void *data, struct wl_keyboard *k, int32_t rate, int32_t delay)
{
    (void)data;
    (void)k;
    (void)rate;
    (void)delay;
}

static const struct wl_keyboard_listener kb_listener = {
    .keymap = kb_keymap,
    .enter = kb_enter,
    .leave = kb_leave,
    .key = kb_key,
    .modifiers = kb_mods,
    .repeat_info = kb_repeat,
};

static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps)
{
    (void)data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && c.pointer == NULL) {
        c.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(c.pointer, &ptr_listener, NULL);
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && c.keyboard == NULL) {
        c.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(c.keyboard, &kb_listener, NULL);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = { .capabilities = seat_caps, .name = seat_name };

static void reg_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
    (void)data;
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        c.compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        c.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
        c.wm = wl_registry_bind(r, name, &xdg_wm_base_interface, version < 3 ? version : 3);
        xdg_wm_base_add_listener(c.wm, &wm_listener, NULL);
    } else if (strcmp(iface, wl_seat_interface.name) == 0) {
        c.seat = wl_registry_bind(r, name, &wl_seat_interface, 3);
        wl_seat_add_listener(c.seat, &seat_listener, NULL);
    }
}

static void reg_remove(void *data, struct wl_registry *r, uint32_t name)
{
    (void)data;
    (void)r;
    (void)name;
}

static const struct wl_registry_listener reg_listener = { .global = reg_global, .global_remove = reg_remove };

/* ----------------------------------------------------------- commands */

static void command(char *line)
{
    int x = 0, y = 0, w = 0, h = 0;
    char adj[16] = "slide";
    if (sscanf(line, "dialog %d %d", &w, &h) == 2) {
        if (c.dialog.surface != NULL) destroy_surf(&c.dialog);
        make_toplevel(&c.dialog, "Dialog", "fmrb-dialog", w, h, 0xFF0000FFu, c.parent.top);
    } else if (sscanf(line, "popup %d %d %d %d %15s", &x, &y, &w, &h, adj) >= 4) {
        make_popup(x, y, w, h, strcmp(adj, "none") != 0);
    } else if (strcmp(line, "unpopup") == 0) {
        if (c.popup.surface != NULL) destroy_surf(&c.popup);
    } else if (strcmp(line, "quit") == 0) {
        c.quit = true;
    } else {
        say("error: unknown line %s", line);
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: wl_dialog FIFO LOG\n");
        return 2;
    }
    c.log = fopen(argv[2], "a");
    if (c.log == NULL) return 1;
    int fifo = open(argv[1], O_RDWR | O_NONBLOCK);
    if (fifo < 0) {
        say("error: %s: %s", argv[1], strerror(errno));
        return 1;
    }
    c.parent.name = "parent";
    c.dialog.name = "dialog";
    c.popup.name = "popup";
    c.display = wl_display_connect(NULL);
    if (c.display == NULL) {
        say("error: no Wayland display");
        return 1;
    }
    struct wl_registry *reg = wl_display_get_registry(c.display);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(c.display);
    if (c.compositor == NULL || c.shm == NULL || c.wm == NULL) {
        say("error: the compositor lacks a global");
        return 1;
    }
    make_toplevel(&c.parent, "Parent", "fmrb-parent", 300, 150, 0xFF00C000u, NULL);
    say("started");

    char buf[512];
    size_t used = 0;
    while (!c.quit) {
        wl_display_flush(c.display);
        struct pollfd pf[2] = { { wl_display_get_fd(c.display), POLLIN, 0 }, { fifo, POLLIN, 0 } };
        if (poll(pf, 2, 200) < 0 && errno != EINTR) break;
        if (pf[0].revents & POLLIN) {
            if (wl_display_dispatch(c.display) < 0) break;
        } else {
            wl_display_dispatch_pending(c.display);
        }
        if (pf[0].revents & (POLLERR | POLLHUP)) break;
        if (pf[1].revents & POLLIN) {
            ssize_t n = read(fifo, buf + used, sizeof(buf) - 1 - used);
            if (n > 0) used += (size_t)n;
            char *nl;
            while ((nl = memchr(buf, '\n', used)) != NULL) {
                *nl = '\0';
                command(buf);
                size_t rest = used - (size_t)(nl + 1 - buf);
                memmove(buf, nl + 1, rest);
                used = rest;
            }
            if (used == sizeof(buf) - 1) used = 0;
        }
    }
    say("end");
    return 0;
}
