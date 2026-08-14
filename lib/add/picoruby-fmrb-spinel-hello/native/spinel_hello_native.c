/*
 * The receiver for the minimal Spinel sample: an mruby task builds a Spinel
 * instance beside its own VM, calls spinel_hello_entry() the way it would call
 * a C function, and tears the instance down. That works because an instance is
 * per-task state (fmrb_spinel_host.h) and mruby's mrb_state knows nothing about
 * it. See doc/spinel_aot/adding_a_spinel_gem.md.
 *
 * The entry takes no arguments and returns an int, so the greeting crosses back
 * through the one FFI function below (spinel_hello_ffi.rb).
 *
 * No FMRB_..._SPINEL opt-out flag: this sample is Spinel-only, so it is always
 * compiled (there is no stub half). It compiles into main, which has the
 * fmrb_* headers on its include path.
 */
#include <stdint.h>
#include <string.h>

#include "spinel_hello_native.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_spinel_host.h"

static const char *TAG = "spinel_hello";

/* Generated from spinel/spinel_hello_entry.rb by rake spinel:gen, compiled with
   --entry spinel_hello_entry. */
extern int spinel_hello_entry(void);

/* The greeting itself is tiny, but a Spinel instance needs a baseline heap for
   its own bootstrap (class table, interned symbols, string heap) before any
   Ruby runs; 16 KB is not enough (instance creation returns NULL). 64 KB is
   comfortable and still small next to the FFT gem's 192 KB. */
#define SH_POOL_BYTES (64 * 1024)
#define SH_OUT_MAX    64

static void *s_pool;
static void *s_est;
static int   s_open;

/* What the entry writes back through the FFI, valid until the next run. */
static char  s_out[SH_OUT_MAX];
static int   s_out_len;

int spinel_hello_available(void)
{
    return 1;
}

int spinel_hello_begin(void)
{
    if (s_open) {
        return 0;
    }
    s_pool = fmrb_sys_malloc(SH_POOL_BYTES);
    if (!s_pool) {
        FMRB_LOGE(TAG, "no memory for the Spinel Hello pool (%d bytes)", SH_POOL_BYTES);
        return -2;
    }
    size_t threshold = SH_POOL_BYTES / 32;
    s_est = fmrb_spinel_instance_begin(s_pool, SH_POOL_BYTES, threshold, threshold);
    if (!s_est) {
        FMRB_LOGE(TAG, "could not create the Spinel Hello instance");
        fmrb_sys_free(s_pool);
        s_pool = NULL;
        return -3;
    }
    s_open = 1;
    FMRB_LOGI(TAG, "Spinel Hello instance up (%d bytes)", SH_POOL_BYTES);
    return 0;
}

const char *spinel_hello_run(int *len_out)
{
    if (!s_open) {
        if (len_out) *len_out = 0;
        return NULL;
    }
    s_out_len = 0;
    int rc = spinel_hello_entry();
    if (rc != 0) {
        FMRB_LOGW(TAG, "Spinel Hello entry returned %d", rc);
    }
    if (len_out) *len_out = s_out_len;
    return s_out;
}

void spinel_hello_end(void)
{
    if (!s_open) {
        return;
    }
    fmrb_spinel_instance_end(s_est);
    s_est = NULL;
    fmrb_sys_free(s_pool);
    s_pool = NULL;
    s_open = 0;
}

/* ---- FFI surface seen by spinel_hello_entry.rb ------------------------- */

/* The entry hands back its greeting here as a byte String plus length. */
void spinel_hello_spx_output(const char *s, int len)
{
    if (!s || len <= 0) {
        s_out_len = 0;
        return;
    }
    if (len > SH_OUT_MAX) {
        len = SH_OUT_MAX;
    }
    memcpy(s_out, s, (size_t)len);
    s_out_len = len;
}
