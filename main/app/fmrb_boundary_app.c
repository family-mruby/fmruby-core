/*
 * The app boundary's FmrbApp flat functions (doc/app_boundary): the C bodies
 * behind fmrb_gen/fmrb_boundary.h, generated from sig/fmrb_app.rbs. Every
 * engine's glue (mruby, Lua, MicroPython, Spinel, WAMR) is generated and
 * calls these; nothing here knows an engine. FmrbApp's functions take no
 * receiver number (the receiver is the calling app).
 *
 * For now the clipboard and the clocks (W4b-2); the rest of FmrbApp comes
 * with W4b-3 (tools/boundary/gen.rb, STAGED).
 *
 * Results follow fmrb_gen/fmrb_boundary.h: 0 or more on success,
 * FMRB_BOUNDARY_ERR_FAILED, or FMRB_BOUNDARY_ERR_ARG for an argument out of
 * range.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fmrb_gen/fmrb_boundary.h"
#include "fmrb_clipboard.h"
#include "fmrb_err.h"
#include "fmrb_hal_time.h"

#define ERR_FAILED FMRB_BOUNDARY_ERR_FAILED
#define ERR_ARG    FMRB_BOUNDARY_ERR_ARG

/* ---- the clipboard (doc/linux_native L12a) -------------------------------- */

/* The first n bytes of b without a last character that is not all there. */
static size_t utf8_whole(const unsigned char *b, size_t n)
{
    size_t i = n;
    for (int k = 0; i > 0 && k < 4; k++) {
        i--;
        if ((b[i] & 0xC0) != 0x80) {
            break;
        }
    }
    if (i >= n) {
        return n;
    }
    unsigned char c = b[i];
    size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    return (n - i >= need) ? n : i;
}

// The text every app shares, at most limit bytes when limit > 0 (cut before
// a character that would not fit whole). Read under the clipboard's lock, so
// one call sees one version. Returns the length of the whole answer; a
// buffer too small for it gets as many whole characters as fit (the glue
// asks again with the length, and a clipboard that changed in between still
// gives text that is not cut inside a character).
int32_t fmrb_bnd_app_clipboard_raw(int32_t limit, char *out, int32_t out_size)
{
    if (out_size < 0 || (out_size > 0 && !out)) {
        return ERR_ARG;
    }
    size_t len = 0;
    const char *text = fmrb_clipboard_lock(&len);
    if (!text) {
        return 0;
    }
    size_t want = (limit > 0 && (size_t)limit < len) ? (size_t)limit : len;
    if (want < len) {
        want = utf8_whole((const unsigned char *)text, want);
    }
    size_t n = want <= (size_t)out_size ? want : utf8_whole((const unsigned char *)text, (size_t)out_size);
    if (n > 0) {
        memcpy(out, text, n);
    }
    fmrb_clipboard_unlock();
    return want > INT32_MAX ? INT32_MAX : (int32_t)want;
}

// false (0) when the text is over the clipboard's maximum (64 KB).
int32_t fmrb_bnd_app_set_clipboard(const char *text, int32_t text_len)
{
    if (!text || text_len < 0) {
        return ERR_ARG;
    }
    return fmrb_clipboard_set(text, (size_t)text_len) == FMRB_OK ? 1 : 0;
}

int32_t fmrb_bnd_app_clipboard_gen(void)
{
    return (int32_t)(fmrb_clipboard_generation() & 0x7fffffffu);
}

/* ---- the clocks ------------------------------------------------------------ */

// Microseconds since boot, for timing short sections (64 bits: it passes
// int32 after 35 minutes).
int64_t fmrb_bnd_app_uptime_us(void)
{
    return (int64_t)fmrb_hal_time_get_us();
}

// The date and time now, local; 0 (nil) when the clock has never been set.
int32_t fmrb_bnd_app_wallclock(fmrb_bnd_app_wallclock_t *out)
{
    fmrb_wallclock_t wc;
    if (fmrb_hal_time_get_wallclock(&wc) != FMRB_OK) {
        return 0;
    }
    out->year = wc.year;
    out->month = wc.month;
    out->day = wc.day;
    out->hour = wc.hour;
    out->minute = wc.minute;
    out->second = wc.second;
    return 1;
}
