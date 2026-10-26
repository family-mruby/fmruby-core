/*
 * input_native.c - the native program's input sources (native_input.h).
 *
 * Both tasks poll without blocking and wait with a FreeRTOS delay: on the
 * cooperative port a task sitting in recv() would stop every other task.
 */

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "fmrb_err.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include "fmrb_keymap.h"
/* main/include: the simulation's packet types (components/fmrb_msg has a
 * header of the same name, the decoder, earlier on the include path). */
#include "../../main/include/fmrb_hid_event.h"
#include "host_task.h"
#include "native_input.h"

static const char *TAG = "input_native";

#define POLL_MS 10

/* ---- sdl: the ring the main thread fills ------------------------------- */

#define RING_EVENTS 256

typedef struct {
    uint32_t type, a, b, c, d;
} native_event_t;

static native_event_t s_ring[RING_EVENTS];
static atomic_uint s_wr;   /* the main thread's */
static unsigned s_rd;      /* the source task's */

void native_input_push(uint32_t type, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    unsigned wr = atomic_load_explicit(&s_wr, memory_order_relaxed);
    native_event_t *ev = &s_ring[wr % RING_EVENTS];
    ev->type = type;
    ev->a = a;
    ev->b = b;
    ev->c = c;
    ev->d = d;
    atomic_store_explicit(&s_wr, wr + 1, memory_order_release);
}

static void dispatch(const native_event_t *ev)
{
    switch (ev->type) {
    case NATIVE_INPUT_KEY_DOWN:
        fmrb_host_send_key_down((uint8_t)ev->a, (uint8_t)ev->a, (uint8_t)ev->b);
        break;
    case NATIVE_INPUT_KEY_UP:
        fmrb_host_send_key_up((uint8_t)ev->a, (uint8_t)ev->a, (uint8_t)ev->b);
        break;
    case NATIVE_INPUT_MOUSE_BUTTON:
        fmrb_host_send_mouse_click((uint16_t)ev->a, (uint16_t)ev->b,
                                   (uint8_t)ev->c, (uint8_t)ev->d);
        break;
    case NATIVE_INPUT_MOUSE_MOVE:
        fmrb_host_send_mouse_move((uint16_t)ev->a, (uint16_t)ev->b);
        break;
    case NATIVE_INPUT_MOUSE_WHEEL:
        fmrb_host_send_mouse_wheel((uint16_t)ev->a, (uint16_t)ev->b,
                                   (int)(int32_t)ev->c);
        break;
    default:
        FMRB_LOGW(TAG, "unknown input event type %u", (unsigned)ev->type);
        break;
    }
}

static void sdl_task(void *arg)
{
    (void)arg;
    for (;;) {
        unsigned wr = atomic_load_explicit(&s_wr, memory_order_acquire);
        if (wr - s_rd > RING_EVENTS) {
            /* The window outran us; drop the overwritten oldest events. */
            s_rd = wr - RING_EVENTS;
        }
        while (s_rd != wr) {
            native_event_t ev = s_ring[s_rd % RING_EVENTS];
            s_rd++;
            dispatch(&ev);
        }
        fmrb_task_delay_ms(POLL_MS);
    }
}

static fmrb_err_t sdl_start(void)
{
    if (fmrb_task_create(sdl_task, "input_sdl", 16384, NULL, 5, NULL) != FMRB_PASS) {
        FMRB_LOGE(TAG, "failed to create the SDL input task");
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

const fmrb_input_source_t fmrb_input_source_sdl = {
    .name  = "SDL window",
    .start = sdl_start,
};

/* ---- inject: the simulation's packets on a socket of our own ----------- */

static char s_inject_path[108];
static int s_inject_fd = -1;

void native_input_set_inject_path(const char *path)
{
    snprintf(s_inject_path, sizeof(s_inject_path), "%s", path ? path : "");
}

/* The sender (tools/fmrb_input.rb) writes SDL's keysym.mod low byte, as
 * sdl2-display forwards it to the simulation; usb_task_linux.c makes the same
 * conversion. ALT is not in that byte. */
static uint8_t sdl_mod_to_fmrb(uint8_t sdl_mod)
{
    uint8_t f = 0;
    if (sdl_mod & 0x01) f |= FMRB_KEYMAP_MOD_LSHIFT;
    if (sdl_mod & 0x02) f |= FMRB_KEYMAP_MOD_RSHIFT;
    if (sdl_mod & 0x40) f |= FMRB_KEYMAP_MOD_LCTRL;
    if (sdl_mod & 0x80) f |= FMRB_KEYMAP_MOD_RCTRL;
    return f;
}

static void inject_packet(uint8_t type, const uint8_t *data, uint16_t len)
{
    switch (type) {
    case HID_EVENT_KEY_DOWN:
    case HID_EVENT_KEY_UP:
        if (len >= sizeof(hid_keyboard_event_t)) {
            const hid_keyboard_event_t *k = (const hid_keyboard_event_t *)data;
            uint8_t mod = sdl_mod_to_fmrb(k->modifier);
            /* The scancode is the HID usage; the key code the sender put
             * next to it is SDL's keysym, which this build does not use. */
            if (type == HID_EVENT_KEY_DOWN) {
                fmrb_host_send_key_down(k->scancode, k->scancode, mod);
            } else {
                fmrb_host_send_key_up(k->scancode, k->scancode, mod);
            }
        }
        break;
    case HID_EVENT_MOUSE_BUTTON:
        if (len >= sizeof(hid_mouse_button_event_t)) {
            const hid_mouse_button_event_t *m = (const hid_mouse_button_event_t *)data;
            fmrb_host_send_mouse_click(m->x, m->y, m->button, m->state);
        }
        break;
    case HID_EVENT_MOUSE_MOTION:
        if (len >= sizeof(hid_mouse_motion_event_t)) {
            const hid_mouse_motion_event_t *m = (const hid_mouse_motion_event_t *)data;
            fmrb_host_send_mouse_move(m->x, m->y);
        }
        break;
    case HID_EVENT_MOUSE_WHEEL:
        if (len >= sizeof(hid_mouse_wheel_event_t)) {
            const hid_mouse_wheel_event_t *w = (const hid_mouse_wheel_event_t *)data;
            fmrb_host_send_mouse_wheel(w->x, w->y, w->delta);
        }
        break;
    default:
        FMRB_LOGW(TAG, "inject: unknown HID event type 0x%02x", type);
        break;
    }
}

static void inject_task(void *arg)
{
    (void)arg;
    uint8_t buf[512];
    for (;;) {
        for (;;) {
            ssize_t n = recv(s_inject_fd, buf, sizeof(buf), MSG_DONTWAIT);
            if (n < 0) {
                if (errno == EINTR) continue;
                break;   /* EAGAIN: nothing waiting */
            }
            /* One datagram may carry several packets back to back. */
            size_t pos = 0;
            while ((size_t)n - pos >= 3) {
                uint8_t type = buf[pos];
                uint16_t len = (uint16_t)(buf[pos + 1] | (buf[pos + 2] << 8));
                if (pos + 3 + len > (size_t)n) break;
                inject_packet(type, buf + pos + 3, len);
                pos += 3 + (size_t)len;
            }
        }
        fmrb_task_delay_ms(POLL_MS);
    }
}

static fmrb_err_t inject_start(void)
{
    if (s_inject_path[0] == '\0') {
        return FMRB_ERR_INVALID_STATE;
    }
    s_inject_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (s_inject_fd < 0) {
        FMRB_LOGE(TAG, "inject: socket: %s", strerror(errno));
        return FMRB_ERR_FAILED;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", s_inject_path);
    unlink(s_inject_path);
    if (bind(s_inject_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        FMRB_LOGE(TAG, "inject: bind %s: %s", s_inject_path, strerror(errno));
        close(s_inject_fd);
        s_inject_fd = -1;
        return FMRB_ERR_FAILED;
    }
    if (fmrb_task_create(inject_task, "input_inject", 16384, NULL, 5, NULL) != FMRB_PASS) {
        FMRB_LOGE(TAG, "failed to create the inject task");
        close(s_inject_fd);
        s_inject_fd = -1;
        return FMRB_ERR_FAILED;
    }
    FMRB_LOGI(TAG, "inject socket: %s", s_inject_path);
    return FMRB_OK;
}

const fmrb_input_source_t fmrb_input_source_inject = {
    .name  = "inject socket",
    .start = inject_start,
};
