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
