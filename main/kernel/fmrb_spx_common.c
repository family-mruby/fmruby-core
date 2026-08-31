/**
 * @file fmrb_spx_common.c
 * @brief Definitions shared by every Spinel FFI shim (kernel + app/gfx),
 *        compiled whenever ANY Spinel engine is active.
 *
 * Holds the pieces both the kernel VM and app VMs need, so a mixed
 * configuration (mruby kernel + Spinel desktop) -- where fmrb_spx_kernel.c is
 * NOT compiled -- still resolves them:
 *
 * - sp_net_bin_len: the byte-length publisher the runtime's codegen emits an
 *   `extern int sp_net_bin_len` for at every :binstr FFI callsite (normally in
 *   sp_net.c, excluded from the fmruby runtime snapshot).
 * - fmrb_spx_board_millis / fmrb_spx_log_write: generic (non-kernel-specific)
 *   millis + logging used by the Log / Machine modules of both the kernel base
 *   and the app base. They were originally in fmrb_spx_kernel.c; moved here so
 *   the desktop can call them without pulling in the whole kernel shim.
 *
 * The mruby-only build compiles neither this file nor any shim, so nothing
 * references these symbols.
 */
#include <stdint.h>
#include <time.h>
#include "fmrb_log.h"
#include "fmrb_kernel.h"
#include "fmrb_limits.h"
#include "picoruby_fmrb_const.h"

static const char *TAG = "spx";

/* :binstr length publisher (see file comment). */
int sp_net_bin_len = 0;

uint32_t fmrb_spx_board_millis(void)
{
    /* CLOCK_MONOTONIC milliseconds. Portable across the IDF Linux target and
       the ESP32 (esp-idf provides POSIX clock_gettime), avoiding an esp_timer
       component dependency. */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

uint32_t fmrb_spx_board_micros(void)
{
    /* Same clock at microsecond resolution. The editor's key-to-present
       instrumentation measures single-digit milliseconds, which milliseconds
       cannot resolve; it wraps every ~71 minutes, and only differences are
       used. */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u);
}

void fmrb_spx_log_write(int level, const char *msg, int len)
{
    if (!msg || len < 0) {
        return;
    }
    switch (level) {
        case 0:  FMRB_LOGD(TAG, "%.*s", len, msg); break;
        case 2:  FMRB_LOGW(TAG, "%.*s", len, msg); break;
        case 3:  FMRB_LOGE(TAG, "%.*s", len, msg); break;
        default: FMRB_LOGI(TAG, "%.*s", len, msg); break;
    }
}

int fmrb_spx_max_apps(void)
{
    /* The configured app ceiling (system_conf.toml's max_apps, clamped to the
       build's FMRB_MAX_APPS), for the same reason theme_color is here rather
       than baked in at generation time: the Spinel kernel used to carry a
       literal, and the literal went stale. */
    const fmrb_system_config_t *conf = fmrb_kernel_get_config();
    return conf ? (int)conf->max_apps : FMRB_MAX_APPS;
}

int fmrb_spx_wheel_lines(void)
{
    /* FmrbConst::WHEEL_LINES for the Spinel programs, read at start-up like
       the theme colours next door rather than baked in at generation time --
       system_conf.toml's wheel_lines has to reach both engines the same way. */
    return (int)fmrb_wheel_lines_get();
}

int fmrb_spx_theme_color(int index)
{
    /* FmrbConst::THEME_* for the Spinel programs. The mruby module reads
       fmrb_theme_get() when a VM starts, so [theme] in system_conf.toml
       reaches every mruby app; the Spinel constants used to be baked in at
       generation time from const.c's defaults and never saw the file. Both
       engines now read the same struct at start-up. The index follows the
       field order of fmrb_theme_t; an unknown index is the window
       background, the colour nothing is invisible against. */
    const fmrb_theme_t *t = fmrb_theme_get();
    switch (index) {
        case 0: return t->desktop_bg;
        case 1: return t->menu_bg;
        case 2: return t->window_bg;
        case 3: return t->text;
        case 4: return t->text_light;
        case 5: return t->highlight;
        case 6: return t->border;
        case 7: return t->button;
        case 8: return t->dir_color;
        default: return t->window_bg;
    }
}
