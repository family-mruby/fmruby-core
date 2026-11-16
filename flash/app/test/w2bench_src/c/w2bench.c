/*
 * w2bench (C): the W2 speed probe (doc/wasm_apps/report/w2.md).
 * One test per on_update: an integer loop, N fill_rect, N draw_text and N
 * cheap boundary calls, three rounds; each logs its time in microseconds
 * ("W2B c <test> n=<N> us=<us> sum=<v>") and the app stops at the end.
 * Each line is also appended to /tmp/w2b.txt (read over WiFi when the
 * serial log is not at hand).
 * The same program is written in TinyGo, Rust, Zig and Ruby.
 */
#include <stdint.h>
#include "fmrb_boundary.h"
#include "fmrb_sdk_version.h"

FMRB_EVENT_BUFFER();
FMRB_SDK_VERSION_SECTION();

#define WINDOW 0
#define N_INT 100000
#define N_FILL 200
#define N_TEXT 100
#define N_CALL 1000

static int32_t step;

static int32_t len(const char *s) { int32_t n = 0; while (s[n]) n++; return n; }

static char *put_str(char *o, const char *s) { while (*s) *o++ = *s++; return o; }
static char *put_int(char *o, int64_t v)
{
    char t[21]; int n = 0;
    uint64_t u = v < 0 ? (uint64_t)(-v) : (uint64_t)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) *o++ = '-';
    while (n) *o++ = t[--n];
    return o;
}

static void report(const char *test, int32_t n, int64_t us, int64_t sum)
{
    char line[96];
    char *o = put_str(line, "W2B c ");
    o = put_str(o, test);
    o = put_str(o, " n="); o = put_int(o, n);
    o = put_str(o, " us="); o = put_int(o, us);
    o = put_str(o, " sum="); o = put_int(o, sum);
    *o = 0;
    fmrb_log_info_raw(line, len(line));
    char nl[100]; int32_t k = 0; while (line[k]) { nl[k] = line[k]; k++; } nl[k++] = '\n';
    fmrb_file_append("/tmp/w2b.txt", 12, (const uint8_t *)nl, k);
}

void on_create(void) { fmrb_log_info_raw("W2B c start", 11); }

int32_t on_update(void)
{
    int32_t kind = step % 4;
    if (step >= 12) { fmrb_app_stop(); return 1000; }
    step++;
    int64_t t0 = fmrb_app_uptime_us();
    int64_t sum = 0;
    if (kind == 0) {
        int32_t s = 1;
        for (int32_t i = 0; i < N_INT; i++) s = ((s * 31 + i) ^ (s >> 3)) & 0xFFFFF;
        sum = s;
        report("int", N_INT, fmrb_app_uptime_us() - t0, sum);
    } else if (kind == 1) {
        for (int32_t i = 0; i < N_FILL; i++)
            fmrb_gfx_fill_rect(WINDOW, (i * 8) % 160, ((i / 20) * 8) % 64, 8, 8, (i * 7) & 0xFE);
        int64_t us = fmrb_app_uptime_us() - t0;
        fmrb_gfx_present_raw(WINDOW, 0, 0, 0);
        report("fill", N_FILL, us, 0);
    } else if (kind == 2) {
        for (int32_t i = 0; i < N_TEXT; i++)
            fmrb_gfx_draw_text_raw(WINDOW, (i % 4) * 40, ((i / 4) % 6) * 10, "0123456789", 10, 0xFF, -1, 0);
        int64_t us = fmrb_app_uptime_us() - t0;
        fmrb_gfx_present_raw(WINDOW, 0, 0, 0);
        report("text", N_TEXT, us, 0);
    } else {
        for (int32_t i = 0; i < N_CALL; i++) sum += fmrb_gfx_area_width(WINDOW);
        report("call", N_CALL, fmrb_app_uptime_us() - t0, sum);
    }
    return 200;
}
