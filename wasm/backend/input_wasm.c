/*
 * input_wasm.c - the browser's input source (doc/wasm/ P4b;
 * hosted/backend/input_source.h).
 *
 * The browser's key and mouse handlers write fixed-size records into a ring
 * in wasm memory (Atomics on the write counter); the poller task drains it
 * into fmrb_host_send_*, the same funnel usb_task_linux.c feeds from its
 * socket. Key semantics follow rd_input.c, the proven browser bridge:
 * key_code = scancode = HID usage ID, modifiers already FMRB_KEYMAP_MOD_*.
 *
 * The usb_task_* surface around it is hosted/backend/input_hosted.c.
 */

#include <string.h>

#include "fmrb_err.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include "input_source.h"
#include "host_task.h"
#include "input_wasm.h"

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
    INPUT_WASM_TEXT,           /* a=bytes (1-12), b..d the UTF-8 bytes, little
                                  end first: text the browser's IME committed
                                  (doc/kanji_input K4). A longer text is
                                  several records in order. */
    INPUT_WASM_IME_GUESS,      /* a=1 the browser's IME composes, 0 a key went
                                  past it: the page's guess at whether it is
                                  on (doc/kanji_input K4b) */
};

#define INPUT_WASM_TEXT_MAX 12

static input_wasm_event_t s_ring[INPUT_WASM_RING_EVENTS];
static volatile uint32_t s_wr = 0;   /* JS bumps with Atomics.add after writing */
static uint32_t s_rd = 0;

/* ---- the JS-facing surface --------------------------------------------- */

EMSCRIPTEN_KEEPALIVE const void *fmrb_wasm_input_ring(void) { return s_ring; }
EMSCRIPTEN_KEEPALIVE uint32_t fmrb_wasm_input_ring_events(void) { return INPUT_WASM_RING_EVENTS; }
EMSCRIPTEN_KEEPALIVE volatile uint32_t *fmrb_wasm_input_wr_ptr(void) { return &s_wr; }

/* The focused app's text cursor for the page (doc/kanji_input K4): seq is odd
 * while it is being written and moves on only when something changed; on is
 * 0 when the app takes no text now (the page then gives the keys to the
 * screen itself, as before). Frame coordinates. */
typedef struct {
    volatile uint32_t seq;
    int32_t on, x, y, h;
} input_wasm_text_cursor_t;

static input_wasm_text_cursor_t s_text_cursor;

EMSCRIPTEN_KEEPALIVE const void *fmrb_wasm_text_cursor(void) { return &s_text_cursor; }

void fmrb_wasm_ime_publish_cursor(bool on, int fx, int fy, int h)
{
    if (!on) fx = fy = h = 0;
    if (s_text_cursor.on == (int32_t)on && s_text_cursor.x == fx &&
        s_text_cursor.y == fy && s_text_cursor.h == h) {
        return;
    }
    __atomic_add_fetch(&s_text_cursor.seq, 1, __ATOMIC_SEQ_CST);   /* odd */
    s_text_cursor.on = on ? 1 : 0;
    s_text_cursor.x = fx;
    s_text_cursor.y = fy;
    s_text_cursor.h = h;
    __atomic_add_fetch(&s_text_cursor.seq, 1, __ATOMIC_SEQ_CST);   /* even */
}

/* The page's last guess at the browser's IME, for the host task: the
 * sequence moves on with every guess, even a repeated one. */
static volatile uint32_t s_ime_guess_seq;
static volatile bool s_ime_guess_on;

bool fmrb_wasm_ime_guess(uint32_t *seq)
{
    uint32_t s1;
    bool on;
    do {
        s1 = s_ime_guess_seq;
        on = s_ime_guess_on;
    } while (s1 != s_ime_guess_seq);
    *seq = s1;
    return on;
}

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
    case INPUT_WASM_TEXT: {
        uint32_t n = ev->a > INPUT_WASM_TEXT_MAX ? INPUT_WASM_TEXT_MAX : ev->a;
        const uint32_t words[3] = { ev->b, ev->c, ev->d };
        char bytes[INPUT_WASM_TEXT_MAX];
        for (uint32_t i = 0; i < n; i++) {
            bytes[i] = (char)((words[i / 4] >> (8 * (i % 4))) & 0xFF);
        }
        if (n > 0 && fmrb_host_send_text(bytes, (int)n) < 0) {
            FMRB_LOGW(TAG, "host queue full: %u bytes of IME text dropped", (unsigned)n);
        }
        break;
    }
    case INPUT_WASM_IME_GUESS:
        s_ime_guess_on = ev->a != 0;
        s_ime_guess_seq++;
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

static fmrb_err_t input_wasm_start(void)
{
    if (fmrb_task_create(input_wasm_task, "input_wasm", 65536, NULL, 5, NULL)
            != FMRB_PASS) {
        FMRB_LOGE(TAG, "failed to create input task");
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

const fmrb_input_source_t fmrb_input_source_wasm = {
    .name  = "browser event ring",
    .start = input_wasm_start,
};
