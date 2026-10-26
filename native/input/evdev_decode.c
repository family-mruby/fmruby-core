/*
 * evdev_decode.c - see evdev_decode.h.
 */

#include "evdev_decode.h"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

#include "fmrb_keymap.h"
#include "linux_keys.h"

#define SHIFT_BITS (FMRB_KEYMAP_MOD_LSHIFT | FMRB_KEYMAP_MOD_RSHIFT)

void evdev_decoder_init(evdev_decoder_t *d, evdev_pointer_t *pointer, fmrb_keymap_extra_fn extra)
{
    memset(d, 0, sizeof(*d));
    d->pointer = pointer;
    d->extra = extra;
}

static void decode_key(evdev_decoder_t *d, unsigned code, int value,
                       const evdev_sink_t *sink, void *ud)
{
    if (value == 2) return;                 /* auto-repeat */
    bool down = value != 0;

    uint8_t bit = fmrb_linux_key_mod_bit(code);
    if (bit != 0) {
        if (down) d->mods |= bit;
        else d->mods &= (uint8_t)~bit;
    }

    if (d->extra != NULL) {
        uint8_t usage = 0;
        bool shift = false;
        if (d->extra(code, &usage, &shift)) {
            if (usage == 0) return;         /* a layer key */
            uint8_t mods = (uint8_t)((d->mods & ~SHIFT_BITS) | (shift ? FMRB_KEYMAP_MOD_LSHIFT : 0));
            if (sink->key) sink->key(ud, down, usage, mods, true);
            return;
        }
    }
    uint8_t usage = fmrb_linux_key_to_hid(code);
    if (usage == 0) return;
    if (sink->key) sink->key(ud, down, usage, d->mods, d->extra != NULL);
}

static int button_of(unsigned code)
{
    switch (code) {
    case BTN_LEFT:   return 1;
    case BTN_MIDDLE: return 2;
    case BTN_RIGHT:  return 3;
    default:         return 0;
    }
}

static void clamp_pointer(evdev_pointer_t *p, const evdev_geometry_t *geo)
{
    if (p->x >= geo->screen_w) p->x = geo->screen_w - 1;
    if (p->y >= geo->screen_h) p->y = geo->screen_h - 1;
    if (p->x < 0) p->x = 0;
    if (p->y < 0) p->y = 0;
}

static void flush_report(evdev_decoder_t *d, const evdev_geometry_t *geo,
                         const evdev_sink_t *sink, void *ud)
{
    evdev_pointer_t *p = d->pointer;
    bool have_screen = geo->screen_w > 0 && geo->screen_h > 0 && geo->base_w > 0 && geo->base_h > 0;
    if (have_screen && !p->placed && (d->dx || d->dy || d->n_buttons || d->wheel)) {
        p->x = geo->screen_w / 2;
        p->y = geo->screen_h / 2;
        p->placed = true;
    }
    if (have_screen && (d->dx || d->dy)) {
        p->ax += d->dx * geo->scale_x * geo->screen_w / geo->base_w;
        p->ay += d->dy * geo->scale_y * geo->screen_h / geo->base_h;
        double ix = 0, iy = 0;
        p->ax = modf(p->ax, &ix);
        p->ay = modf(p->ay, &iy);
        if ((int)ix != 0 || (int)iy != 0) {
            p->x += (int)ix;
            p->y += (int)iy;
            clamp_pointer(p, geo);
            if (sink->move) sink->move(ud, p->x, p->y);
        }
    }
    if (have_screen) {
        clamp_pointer(p, geo);
        for (int i = 0; i < d->n_buttons; i++) {
            if (sink->button) sink->button(ud, p->x, p->y, d->buttons[i].button, d->buttons[i].down);
        }
        if (d->wheel != 0 && sink->wheel) {
            int n = d->wheel;
            if (n > 127) n = 127;
            if (n < -127) n = -127;
            sink->wheel(ud, p->x, p->y, n);
        }
    }
    d->dx = d->dy = d->wheel = 0;
    d->n_buttons = 0;
}

void evdev_decode(evdev_decoder_t *d, const struct input_event *ev,
                  const evdev_geometry_t *geo, const evdev_sink_t *sink, void *ud)
{
    switch (ev->type) {
    case EV_KEY: {
        int b = button_of(ev->code);
        if (b != 0) {
            if (ev->value != 2 && d->n_buttons < EVDEV_PENDING_BUTTONS) {
                d->buttons[d->n_buttons].button = b;
                d->buttons[d->n_buttons].down = ev->value != 0;
                d->n_buttons++;
            }
        } else if (ev->code < BTN_MISC) {
            decode_key(d, ev->code, ev->value, sink, ud);
        }
        break;
    }
    case EV_REL:
        if (ev->code == REL_X) d->dx += ev->value;
        else if (ev->code == REL_Y) d->dy += ev->value;
        else if (ev->code == REL_WHEEL) d->wheel += ev->value;
        break;
    case EV_SYN:
        if (ev->code == SYN_REPORT) {
            flush_report(d, geo, sink, ud);
        } else if (ev->code == SYN_DROPPED) {
            /* The kernel's buffer overflowed: what is pending is partial. */
            d->dx = d->dy = d->wheel = 0;
            d->n_buttons = 0;
        }
        break;
    default:
        break;
    }
}

int evdev_drain_fd(int fd, evdev_decoder_t *d, const evdev_geometry_t *geo,
                   const evdev_sink_t *sink, void *ud)
{
    struct input_event evs[64];
    int total = 0;
    for (;;) {
        ssize_t n = read(fd, evs, sizeof(evs));
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return total;
            return -1;                       /* ENODEV: unplugged */
        }
        if (n == 0) return total > 0 ? total : -1;
        int count = (int)((size_t)n / sizeof(evs[0]));
        for (int i = 0; i < count; i++) evdev_decode(d, &evs[i], geo, sink, ud);
        total += count;
        if ((size_t)n < sizeof(evs)) return total;
    }
}
