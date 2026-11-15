/*
 * c_check: a wasm app in C on the firmware's own path (doc/wasm_apps W1b,
 * decisions 4, 5 and 12). It calls the generated SDK header's functions
 * directly, so a fault here is the firmware's side, not an SDK's. It draws,
 * logs, reads the clock, checks that a pointer outside its memory is
 * refused (FMRB_BOUNDARY_ERR_ARG), counts its updates and events and shows
 * the last event's record (on_event takes fmrb_app_event_t *). It reads the
 * window's user area with the FmrbApp functions that need the app shell,
 * and q ends it with fmrb_app_stop().
 * Nothing it calls changes the machine's settings (no volume, no clipboard).
 * Built by tools/wasm_apps/build.rb (zig cc) into flash/app/test.
 */
#include <stdint.h>
#include "fmrb_boundary.h"

#define EXPORT(name) __attribute__((export_name(name)))

/* The place the firmware writes on_event's record into (decision 20). */
FMRB_EVENT_BUFFER();

static int32_t frames, events, bad_ptr;
static fmrb_app_event_t last;

static void log_str(const char *s)
{
    int32_t n = 0;
    while (s[n]) n++;
    fmrb_log_info_raw(s, n);
}

static int32_t fmt(char *buf, const char *label, int32_t v)
{
    int32_t n = 0;
    while (label[n]) { buf[n] = label[n]; n++; }
    char tmp[12];
    int32_t t = 0;
    uint32_t u = v < 0 ? (uint32_t)(-v) : (uint32_t)v;
    do { tmp[t++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) buf[n++] = '-';
    while (t) buf[n++] = tmp[--t];
    return n;
}

EXPORT("on_create") void on_create(void)
{
    /* A pointer far outside the app's memory: refused before the body runs. */
    bad_ptr = fmrb_gfx_draw_text_raw(0, 0, 0, (const char *)0xfffffff0u, 16, 0, -1, 0);
    log_str(bad_ptr == FMRB_BOUNDARY_ERR_ARG ? "c_check: bad pointer refused (ok)"
                                             : "c_check: bad pointer NOT refused");
}

EXPORT("on_update") int32_t on_update(void)
{
    char line[40];
    int32_t w = fmrb_gfx_area_width(0), h = fmrb_gfx_area_height(0);
    fmrb_gfx_fill_rect(0, 0, 0, w, h, fmrb_gfx_rgb_to_332(32, 32, 32));
    fmrb_gfx_draw_text_raw(0, 2, 2, "C on WAMR", 9, 0xff, -1, 0);
    fmrb_gfx_draw_text_raw(0, 2, 12, line, fmt(line, "frame ", frames), 0xff, -1, 0);
    fmrb_gfx_draw_text_raw(0, 2, 22, line, fmt(line, "events ", events), 0xff, -1, 0);
    fmrb_gfx_draw_text_raw(0, 2, 32, line,
                           fmt(line, "up s ", (int32_t)(fmrb_app_uptime_us() / 1000000)), 0xff, -1, 0);
    fmrb_gfx_draw_text_raw(0, 2, 42, line, fmt(line, "bad ptr ", bad_ptr), 0xff, -1, 0);
    /* The last event: its type, then x, y for the mouse or the character. */
    int32_t n = fmt(line, "ev ", last.type);
    if (last.type <= FMRB_APP_EVENT_TYPE_KEY_UP) {
        n += fmt(line + n, " ch ", last.character);
    } else {
        n += fmt(line + n, " x ", last.x);
        n += fmt(line + n, " y ", last.y);
    }
    fmrb_gfx_draw_text_raw(0, 2, 52, line, n, 0xff, -1, 0);
    /* The shell's user area, read through FmrbApp (needs ctx->shell). */
    n = fmt(line, "area ", fmrb_app_user_area_width());
    n += fmt(line + n, "x", fmrb_app_user_area_height());
    fmrb_gfx_draw_text_raw(0, 2, 62, line, n, 0xff, -1, 0);
    int32_t x = (frames * 3) % (w > 10 ? w - 10 : 1);
    fmrb_gfx_fill_circle(0, x + 5, h - 8, 4, fmrb_gfx_rgb_to_332(255, 160, 0));
    fmrb_gfx_present_raw(0, 0, 0, 0);
    frames++;
    return 100;
}

EXPORT("on_event") void on_event(const fmrb_app_event_t *ev)
{
    last = *ev;
    events++;
    if (ev->type == FMRB_APP_EVENT_TYPE_KEY_DOWN && ev->character == 'q') {
        log_str("c_check: q, stopping");
        fmrb_app_stop();
    }
}

EXPORT("on_destroy") void on_destroy(void)
{
    log_str("c_check: on_destroy");
}
