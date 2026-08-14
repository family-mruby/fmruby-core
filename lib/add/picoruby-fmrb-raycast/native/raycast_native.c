/*
 * The receiver for the Spinel raycaster: an mruby app task builds a Spinel
 * instance beside its own VM, calls raycast_entry() the way it would call a C
 * function, and tears the instance down. That works because an instance is
 * per-task state (fmrb_spinel_host.h) and mruby's mrb_state knows nothing
 * about it. See doc/spinel_aot/adding_a_spinel_gem.md.
 *
 * The entry takes no arguments and returns an int, so the map, the player and
 * the depth buffer all cross through the globals below, published to the Ruby
 * side as FFI calls (spinel/raycast_ffi.rb).
 *
 * Placement: every static here goes to PSRAM on the device. The map buffer
 * alone is 4 KB and internal DRAM is the scarce resource; nothing in this file
 * is on a hardware path that would need internal RAM (no DMA, no assembly
 * kernel), so PSRAM costs nothing but a slower access that is dwarfed by the
 * Ruby doing the casting.
 */
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "raycast_native.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_spinel_host.h"

#ifdef CONFIG_IDF_TARGET_LINUX
#define RC_BSS_ATTR
#else
#include "esp_attr.h"
#define RC_BSS_ATTR EXT_RAM_BSS_ATTR
#endif

static const char *TAG = "raycast";

/* Generated from spinel/raycast_entry.rb by rake spinel:gen, compiled with
   --entry raycast_entry --persistent-statics. */
extern int raycast_entry(void);

/* :binstr length publisher, defined in fmrb_spx_common.c. */
extern int sp_net_bin_len;

/* The core the entry caches holds the map plus two 360-entry trig tables, and
   the transient String per frame on top. 128 KB leaves the collector room to
   work rather than thrash; it comes from fmrb_sys_malloc, which is PSRAM, so a
   demo nobody launches costs no internal RAM. */
#define RC_POOL_BYTES (128 * 1024)

/* 25 rays of 6 bytes today; sized for a wider view without a rebuild. */
#define RC_OUT_MAX 512

RC_BSS_ATTR static void *s_pool;
RC_BSS_ATTR static void *s_est;
RC_BSS_ATTR static int   s_open;

/* The world. Uploaded rarely; s_map_gen changes on every upload, which is how
   the entry knows the core it cached is stale. */
RC_BSS_ATTR static uint8_t s_map[RAYCAST_MAP_MAX];
RC_BSS_ATTR static int     s_map_w;
RC_BSS_ATTR static int     s_map_h;
RC_BSS_ATTR static int     s_map_gen;

/* Per-frame input and output. Valid only inside one run(). */
RC_BSS_ATTR static int      s_px;
RC_BSS_ATTR static int      s_py;
RC_BSS_ATTR static int      s_pa;
RC_BSS_ATTR static char     s_out[RC_OUT_MAX];
RC_BSS_ATTR static int      s_out_len;
RC_BSS_ATTR static uint32_t s_us;

int raycast_available(void)
{
    return 1;
}

uint32_t raycast_micros(void)
{
    /* One clock for both backends, so the two numbers the demo shows mean the
       same thing. The IDF provides clock_gettime on the device as well. */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u);
}

int raycast_begin(void)
{
    if (s_open) {
        return 0;
    }
    s_pool = fmrb_sys_malloc(RC_POOL_BYTES);
    if (!s_pool) {
        FMRB_LOGE(TAG, "no memory for the Spinel raycast pool (%d bytes)", RC_POOL_BYTES);
        return -2;
    }
    /* Same threshold rule as the other Spinel instances: collect early so a
       burst of allocation cannot reach the top of the pool, since exhaustion
       goes through sp_oom_die and takes the firmware with it. */
    size_t threshold = RC_POOL_BYTES / 32;
    s_est = fmrb_spinel_instance_begin(s_pool, RC_POOL_BYTES, threshold, threshold);
    if (!s_est) {
        FMRB_LOGE(TAG, "could not create the Spinel raycast instance");
        fmrb_sys_free(s_pool);
        s_pool = NULL;
        return -3;
    }
    s_open = 1;
    FMRB_LOGI(TAG, "Spinel raycast instance up (%d bytes)", RC_POOL_BYTES);
    return 0;
}

int raycast_set_map(const uint8_t *cells, int w, int h)
{
    if (!cells || w < 1 || h < 1) {
        return -1;
    }
    if (w * h > RAYCAST_MAP_MAX) {
        FMRB_LOGE(TAG, "map %dx%d is larger than the %d cells this holds", w, h, RAYCAST_MAP_MAX);
        return -2;
    }
    memcpy(s_map, cells, (size_t)(w * h));
    s_map_w = w;
    s_map_h = h;
    /* The entry compares this against the generation its cached core carries;
       a change is what makes it rebuild. */
    s_map_gen++;
    return 0;
}

const char *raycast_run(int px, int py, int pa, int *out_len, uint32_t *out_us)
{
    if (out_len) *out_len = 0;
    if (out_us) *out_us = 0;
    if (!s_open || s_map_w < 1) {
        return NULL;
    }
    s_px = px;
    s_py = py;
    s_pa = pa;
    s_out_len = 0;
    s_us = 0;

    int rc = raycast_entry();
    if (rc != 0) {
        FMRB_LOGW(TAG, "Spinel raycast entry returned %d", rc);
    }

    if (out_len) *out_len = s_out_len;
    if (out_us) *out_us = s_us;
    return s_out;
}

void raycast_end(void)
{
    if (!s_open) {
        return;
    }
    fmrb_spinel_instance_end(s_est);
    s_est = NULL;
    fmrb_sys_free(s_pool);
    s_pool = NULL;
    s_open = 0;
    /* The next instance starts from an empty program, so anything it cached is
       gone; make it rebuild rather than trust a generation from last time. */
    s_map_gen++;
}

/* ---- FFI surface seen by raycast_entry.rb -------------------------------- */

const char *raycast_spx_map(void)
{
    sp_net_bin_len = s_map_w * s_map_h;
    return (const char *)s_map;
}

int raycast_spx_map_w(void)   { return s_map_w; }
int raycast_spx_map_h(void)   { return s_map_h; }
int raycast_spx_map_gen(void) { return s_map_gen; }

int raycast_spx_px(void) { return s_px; }
int raycast_spx_py(void) { return s_py; }
int raycast_spx_pa(void) { return s_pa; }

int raycast_spx_micros(void)
{
    return (int)raycast_micros();
}

void raycast_spx_output(const char *buf, int len, int us)
{
    s_us = (uint32_t)us;
    if (!buf || len <= 0) {
        return;
    }
    if (len > RC_OUT_MAX) {
        len = RC_OUT_MAX;
    }
    memcpy(s_out, buf, (size_t)len);
    s_out_len = len;
}

void raycast_spx_log(const char *msg, int len)
{
    if (msg && len > 0) {
        FMRB_LOGI(TAG, "%.*s", len, msg);
    }
}
