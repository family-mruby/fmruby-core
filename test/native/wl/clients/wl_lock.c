/*
 * wl_lock.c - a screen lock program for the compositor's checks
 * (test/native/wl/wl_winops_check.rb, doc/linux_native/ L13): an
 * ext-session-lock-v1 client driven by lines on a FIFO, telling what it gets
 * on a log. It asks for no password: the check unlocks it with a line.
 *
 *   wl_lock [--now] FIFO LOG
 *
 * --now locks at once (as a lock program started by the compositor does).
 * The lines it takes:
 *
 *   lock      lock: a lock surface on every output, magenta
 *   unlock    unlock and end the lock (the screen comes back)
 *   die       end at once without unlocking (a lock program that crashed)
 *   quit      end (the lock, if any, ends unlocked only through "unlock")
 *
 * The log, one line each: "started", "locked", "finished" (the compositor
 * refused or ended the lock), "configure W H" (a lock surface), "key CODE
 * 1|0", "button B 1|0", "kbd" (the keyboard came), "unlocked", "end".
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

#include "ext-session-lock-v1-client-protocol.h"

#define MAX_OUTPUTS 4

typedef struct {
    struct wl_output *output;
    struct wl_surface *surface;
    struct ext_session_lock_surface_v1 *lock_surface;
    struct wl_buffer *buffer;
} out_t;

static struct {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_pointer *pointer;
    struct ext_session_lock_manager_v1 *mgr;
    struct ext_session_lock_v1 *lock;
    out_t out[MAX_OUTPUTS];
    int n_out;
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

static void draw(out_t *o, int w, int h)
{
    int stride = w * 4;
    size_t size = (size_t)stride * (size_t)h;
    int fd = memfd_create("wl_lock", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size) != 0) {
        if (fd >= 0) close(fd);
        return;
    }
    uint32_t *px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (px == MAP_FAILED) {
        close(fd);
        return;
    }
    for (size_t i = 0; i < size / 4; i++) px[i] = 0xFFFF00FFu;
    munmap(px, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, (int32_t)size);
    if (o->buffer != NULL) wl_buffer_destroy(o->buffer);
    o->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_surface_attach(o->surface, o->buffer, 0, 0);
    wl_surface_damage_buffer(o->surface, 0, 0, w, h);
    wl_surface_commit(o->surface);
}

static void ls_configure(void *data, struct ext_session_lock_surface_v1 *ls, uint32_t serial, uint32_t w, uint32_t h)
{
    out_t *o = data;
    ext_session_lock_surface_v1_ack_configure(ls, serial);
    say("configure %u %u", w, h);
    draw(o, (int)w, (int)h);
}

static const struct ext_session_lock_surface_v1_listener ls_listener = { .configure = ls_configure };

static void lock_locked(void *data, struct ext_session_lock_v1 *l)
{
    (void)data;
    (void)l;
    say("locked");
}

static void lock_finished(void *data, struct ext_session_lock_v1 *l)
{
    (void)data;
    say("finished");
    ext_session_lock_v1_destroy(l);
    c.lock = NULL;
}

static const struct ext_session_lock_v1_listener lock_listener = { .locked = lock_locked, .finished = lock_finished };

static void do_lock(void)
{
    if (c.lock != NULL || c.mgr == NULL) return;
    c.lock = ext_session_lock_manager_v1_lock(c.mgr);
    ext_session_lock_v1_add_listener(c.lock, &lock_listener, NULL);
    for (int i = 0; i < c.n_out; i++) {
        out_t *o = &c.out[i];
        o->surface = wl_compositor_create_surface(c.compositor);
        o->lock_surface = ext_session_lock_v1_get_lock_surface(c.lock, o->surface, o->output);
        ext_session_lock_surface_v1_add_listener(o->lock_surface, &ls_listener, o);
    }
}

static void do_unlock(void)
{
    if (c.lock == NULL) return;
    for (int i = 0; i < c.n_out; i++) {
        out_t *o = &c.out[i];
        if (o->lock_surface) ext_session_lock_surface_v1_destroy(o->lock_surface);
        if (o->surface) wl_surface_destroy(o->surface);
        o->lock_surface = NULL;
        o->surface = NULL;
    }
    ext_session_lock_v1_unlock_and_destroy(c.lock);
    c.lock = NULL;
    wl_display_roundtrip(c.display);
    say("unlocked");
}

/* -------------------------------------------------------------- input */

static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t f, int32_t fd, uint32_t sz)
{
    (void)d; (void)k; (void)f; (void)sz;
    close(fd);
}
static void kb_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf, struct wl_array *keys)
{
    (void)d; (void)k; (void)s; (void)sf; (void)keys;
    say("kbd");
}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf)
{
    (void)d; (void)k; (void)s; (void)sf;
}
static void kb_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t key, uint32_t st)
{
    (void)d; (void)k; (void)s; (void)t;
    say("key %u %u", key, st);
}
static void kb_mods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t a, uint32_t b, uint32_t e, uint32_t g)
{
    (void)d; (void)k; (void)s; (void)a; (void)b; (void)e; (void)g;
}
static void kb_repeat(void *d, struct wl_keyboard *k, int32_t r, int32_t dl)
{
    (void)d; (void)k; (void)r; (void)dl;
}
static const struct wl_keyboard_listener kb_listener = { kb_keymap, kb_enter, kb_leave, kb_key, kb_mods, kb_repeat };

static void p_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)p; (void)s; (void)sf; (void)x; (void)y;
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf)
{
    (void)d; (void)p; (void)s; (void)sf;
}
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)p; (void)t; (void)x; (void)y;
}
static void p_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st)
{
    (void)d; (void)p; (void)s; (void)t;
    say("button %u %u", b, st);
}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v)
{
    (void)d; (void)p; (void)t; (void)a; (void)v;
}
static const struct wl_pointer_listener p_listener = { .enter = p_enter, .leave = p_leave, .motion = p_motion,
                                                       .button = p_button, .axis = p_axis };

static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps)
{
    (void)data;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && c.keyboard == NULL) {
        c.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(c.keyboard, &kb_listener, NULL);
    }
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && c.pointer == NULL) {
        c.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(c.pointer, &p_listener, NULL);
    }
}
static void seat_name(void *d, struct wl_seat *s, const char *n)
{
    (void)d; (void)s; (void)n;
}
static const struct wl_seat_listener seat_listener = { .capabilities = seat_caps, .name = seat_name };

static void reg_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
    (void)data;
    (void)version;
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        c.compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        c.shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    } else if (strcmp(iface, wl_seat_interface.name) == 0) {
        c.seat = wl_registry_bind(r, name, &wl_seat_interface, 3);
        wl_seat_add_listener(c.seat, &seat_listener, NULL);
    } else if (strcmp(iface, wl_output_interface.name) == 0 && c.n_out < MAX_OUTPUTS) {
        c.out[c.n_out++].output = wl_registry_bind(r, name, &wl_output_interface, 1);
    } else if (strcmp(iface, ext_session_lock_manager_v1_interface.name) == 0) {
        c.mgr = wl_registry_bind(r, name, &ext_session_lock_manager_v1_interface, 1);
    }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n)
{
    (void)d; (void)r; (void)n;
}
static const struct wl_registry_listener reg_listener = { .global = reg_global, .global_remove = reg_remove };

static void command(char *line)
{
    if (strcmp(line, "lock") == 0) {
        do_lock();
    } else if (strcmp(line, "unlock") == 0) {
        do_unlock();
    } else if (strcmp(line, "die") == 0) {
        say("dying without unlocking");
        _exit(3);
    } else if (strcmp(line, "quit") == 0) {
        c.quit = true;
    } else {
        say("error: unknown line %s", line);
    }
}

int main(int argc, char **argv)
{
    bool now = argc > 1 && strcmp(argv[1], "--now") == 0;
    if (argc != (now ? 4 : 3)) {
        fprintf(stderr, "usage: wl_lock [--now] FIFO LOG\n");
        return 2;
    }
    const char *fifo_path = argv[now ? 2 : 1];
    c.log = fopen(argv[now ? 3 : 2], "a");
    if (c.log == NULL) return 1;
    int fifo = open(fifo_path, O_RDWR | O_NONBLOCK);
    if (fifo < 0) {
        say("error: %s: %s", fifo_path, strerror(errno));
        return 1;
    }
    c.display = wl_display_connect(NULL);
    if (c.display == NULL) {
        say("error: no Wayland display");
        return 1;
    }
    struct wl_registry *reg = wl_display_get_registry(c.display);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(c.display);
    wl_display_roundtrip(c.display);
    if (c.compositor == NULL || c.shm == NULL || c.mgr == NULL) {
        say("error: the compositor lacks a global (ext-session-lock: %s)", c.mgr ? "yes" : "no");
        return 1;
    }
    say("started (%d outputs)", c.n_out);
    if (now) do_lock();
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
