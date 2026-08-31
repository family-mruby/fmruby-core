/*
 * input_wasm.c - the wasm input backend (doc/wasm/ P4b).
 *
 * The browser's key and mouse handlers write fixed-size records into a ring
 * in wasm memory (Atomics on the write counter); the poller task drains it
 * into fmrb_host_send_*, the same funnel usb_task_linux.c feeds from its
 * socket. Key semantics follow rd_input.c, the proven browser bridge:
 * key_code = scancode = HID usage ID, modifiers already FMRB_KEYMAP_MOD_*.
 *
 * The usb_task_* device-list surface stays stubbed: there is no USB bus, so
 * the HID Inspector reports no devices and raw-report subscription fails.
 */

#include <string.h>

#include "fmrb_err.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include "usb_task.h"
#include "host_task.h"

#include <emscripten/emscripten.h>

static const char *TAG = "input_wasm";

/* One record per browser event. All fields u32 so the JS side writes plain
 * HEAPU32 slots; 256 events absorbs any realistic burst between two 10 ms
 * polls. */
#define INPUT_WASM_RING_EVENTS 256

typedef struct {
    uint32_t type;   /* fmrb_wasm_input_type_t */
    uint32_t a, b, c, d;
} input_wasm_event_t;

enum {
    INPUT_WASM_KEY_DOWN = 1,   /* a=scancode (HID usage), b=FMRB_KEYMAP_MOD_* */
    INPUT_WASM_KEY_UP,         /* a=scancode, b=mod */
    INPUT_WASM_MOUSE_BUTTON,   /* a=x, b=y, c=button(1=L 2=M 3=R), d=state */
    INPUT_WASM_MOUSE_MOVE,     /* a=x, b=y */
    INPUT_WASM_MOUSE_WHEEL,    /* a=x, b=y, c=notches (signed, cast from i32) */
};

static input_wasm_event_t s_ring[INPUT_WASM_RING_EVENTS];
static volatile uint32_t s_wr = 0;   /* JS bumps with Atomics.add after writing */
static uint32_t s_rd = 0;

/* ---- the JS-facing surface --------------------------------------------- */

EMSCRIPTEN_KEEPALIVE const void *fmrb_wasm_input_ring(void) { return s_ring; }
EMSCRIPTEN_KEEPALIVE uint32_t fmrb_wasm_input_ring_events(void) { return INPUT_WASM_RING_EVENTS; }
EMSCRIPTEN_KEEPALIVE volatile uint32_t *fmrb_wasm_input_wr_ptr(void) { return &s_wr; }

/* ------------------------------------------------------------------------ */

static void dispatch(const input_wasm_event_t *ev)
{
    switch (ev->type) {
    case INPUT_WASM_KEY_DOWN:
        fmrb_host_send_key_down((uint8_t)ev->a, (uint8_t)ev->a, (uint8_t)ev->b);
        break;
    case INPUT_WASM_KEY_UP:
        fmrb_host_send_key_up((uint8_t)ev->a, (uint8_t)ev->a, (uint8_t)ev->b);
        break;
    case INPUT_WASM_MOUSE_BUTTON:
        fmrb_host_send_mouse_click((uint16_t)ev->a, (uint16_t)ev->b,
                                   (uint8_t)ev->c, (uint8_t)ev->d);
        break;
    case INPUT_WASM_MOUSE_MOVE:
        fmrb_host_send_mouse_move((uint16_t)ev->a, (uint16_t)ev->b);
        break;
    case INPUT_WASM_MOUSE_WHEEL:
        /* c is written as a signed count by the page; the ring is u32. */
        fmrb_host_send_mouse_wheel((uint16_t)ev->a, (uint16_t)ev->b,
                                   (int)(int32_t)ev->c);
        break;
    default:
        FMRB_LOGW(TAG, "unknown input event type %u", (unsigned)ev->type);
        break;
    }
}

static void input_wasm_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t wr = s_wr;
        if (wr - s_rd > INPUT_WASM_RING_EVENTS) {
            /* The browser outran us; drop the overwritten oldest events. */
            s_rd = wr - INPUT_WASM_RING_EVENTS;
        }
        while (s_rd != wr) {
            input_wasm_event_t ev = s_ring[s_rd % INPUT_WASM_RING_EVENTS];
            s_rd++;
            dispatch(&ev);
        }
        fmrb_task_delay_ms(10);
    }
}

fmrb_err_t usb_task_init(void)
{
    if (fmrb_task_create(input_wasm_task, "input_wasm", 65536, NULL, 5, NULL)
            != FMRB_PASS) {
        FMRB_LOGE(TAG, "failed to create input task");
        return FMRB_ERR_FAILED;
    }
    FMRB_LOGI(TAG, "input backend up (browser event ring)");
    return FMRB_OK;
}

fmrb_err_t usb_task_power_on_root_port(void)
{
    return FMRB_OK;
}

void usb_task_start(void)
{
}

void usb_task_stop(void)
{
}

int usb_task_get_device_info(fmrb_usb_device_info_t *out, int max_count)
{
    (void)out;
    (void)max_count;
    return 0;
}

fmrb_err_t usb_task_subscribe_raw_reports(int8_t slot_index, uint16_t subscriber_pid)
{
    (void)slot_index;
    (void)subscriber_pid;
    return FMRB_ERR_NOT_SUPPORTED;
}

fmrb_err_t usb_task_unsubscribe_raw_reports(int8_t slot_index)
{
    (void)slot_index;
    return FMRB_ERR_NOT_SUPPORTED;
}
