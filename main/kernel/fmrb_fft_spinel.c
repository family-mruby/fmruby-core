/*
 * The Spinel FFT backend: an AOT-compiled Ruby program called as a library
 * from a task that is not a Spinel task (doc/mic_spectrum/impl_plan.md).
 *
 * Every other Spinel program in this firmware IS the task -- the kernel, the
 * desktop, the editor each own their instance for their lifetime. Here an
 * mruby app task builds an instance beside its own VM, calls
 * fmrb_fft_spinel_entry() the way it would call a C function, and tears the
 * instance down. That works because an instance is per-task state
 * (fmrb_spinel_host.h) and mruby's mrb_state knows nothing about it: the two
 * runtimes share the task and nothing else.
 *
 * The entry takes no arguments and returns an int, so the samples, the
 * repetition count and the results cross through the four globals below,
 * published to the Ruby side as FFI calls (main/prebuild_scripts/spinel/
 * fmrb_fft_ffi.rb). The Ruby reads its input, times its own loop with the same
 * clock the C backend uses, and hands back the magnitudes.
 *
 * The pool: a Spinel instance allocates everything -- objects, GC roots,
 * strings -- from one estalloc region handed to it at creation. FFT_POOL_BYTES
 * is sized for the tables fft_core.rb builds (six arrays of `n` Floats for
 * n=1024, plus the strings crossing the boundary) with room for the collector
 * to work in, and it comes out of the calling task's own heap, so a benchmark
 * that is never run costs nothing.
 */

#include <stdint.h>
#include <string.h>

#include "fmrb_fft_spinel.h"
#include "fmrb_fft_bench.h"

#ifdef FMRB_FFT_SPINEL

#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_spinel_host.h"

static const char *TAG = "fft_spx";

/* Generated from main/prebuild_scripts/spinel/fft_spinel.rb by rake spinel:gen
   and compiled with --entry fmrb_fft_spinel_entry. */
extern int fmrb_fft_spinel_entry(void);

/* :binstr length publisher, defined in fmrb_spx_common.c. */
extern int sp_net_bin_len;

/* Pool for the Spinel instance. 1024 points needs six Float arrays of 1024
   (~8KB each once boxed) plus the sample and magnitude Strings; 192KB leaves
   the collector room to run rather than thrash. Measured, not guessed: see
   doc/mic_spectrum/report/. */
#define FFT_POOL_BYTES (192 * 1024)

/* Single-owner-task constraint: the instance and the run I/O below are
   file-scope statics, and the instance is current on whichever task called
   begin(). This backend therefore supports one instance owned by one task;
   calling it concurrently from another task corrupts the run globals and uses
   an instance that is not current there. Fine for a single benchmark/visualizer
   app; a future concurrent user must serialize onto the owner task. */
static void    *s_pool;
static void    *s_est;
static int      s_open;

/* What the entry reads and writes. Valid only inside one run. */
static const int16_t *s_in;
static int             s_n;
static int             s_iters;
static int16_t        *s_mag;
static int             s_mag_len;
static uint32_t        s_us;

int fmrb_fft_spinel_available(void)
{
    return 1;
}

int fmrb_fft_spinel_begin(int size)
{
    if (s_open) {
        return 0;   /* already up; the size travels per run */
    }
    if (size < 64 || size > FMRB_FFT_MAX_N || (size & (size - 1)) != 0) {
        return -1;
    }

    s_pool = fmrb_sys_malloc(FFT_POOL_BYTES);
    if (!s_pool) {
        FMRB_LOGE(TAG, "no memory for the Spinel FFT pool (%d bytes)", FFT_POOL_BYTES);
        return -2;
    }
    /* Same threshold rule as the kernel instance (fmrb_kernel.c): collect early
       so a burst of allocation cannot reach the top of the pool, since
       exhaustion goes through sp_oom_die and takes the firmware with it. */
    size_t threshold = FFT_POOL_BYTES / 32;
    s_est = fmrb_spinel_instance_begin(s_pool, FFT_POOL_BYTES, threshold, threshold);
    if (!s_est) {
        FMRB_LOGE(TAG, "could not create the Spinel FFT instance");
        fmrb_sys_free(s_pool);
        s_pool = NULL;
        return -3;
    }
    s_open = 1;
    FMRB_LOGI(TAG, "Spinel FFT instance up (%d bytes, size %d)", FFT_POOL_BYTES, size);
    return 0;
}

uint32_t fmrb_fft_spinel_run(const int16_t *in, int n, int iters, int16_t *mag_out)
{
    if (!s_open || !in || iters < 1) {
        return 0;
    }
    s_in = in;
    s_n = n;
    s_iters = iters;
    s_mag = mag_out;
    s_mag_len = 0;
    s_us = 0;

    int rc = fmrb_fft_spinel_entry();
    if (rc != 0) {
        FMRB_LOGW(TAG, "Spinel FFT entry returned %d", rc);
    }

    s_in = NULL;
    s_mag = NULL;
    return s_us;
}

void fmrb_fft_spinel_end(void)
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

/* ---- FFI surface seen by fft_spinel.rb ---------------------------------- */

/* The samples as a byte String (:binstr): the Ruby decodes them exactly as the
   mruby side does, from the same bytes. */
const char *fmrb_fft_spx_samples(void)
{
    if (!s_in) {
        sp_net_bin_len = 0;
        return "";
    }
    sp_net_bin_len = s_n * 2;
    return (const char *)s_in;
}

int fmrb_fft_spx_size(void)
{
    return s_n;
}

int fmrb_fft_spx_iters(void)
{
    return s_iters;
}

/* The same clock the C backend uses, so the two numbers mean the same thing. */
int fmrb_fft_spx_micros(void)
{
    return (int)fmrb_fft_micros();
}

void fmrb_fft_spx_output(const char *mag, int len, int us)
{
    s_us = (uint32_t)us;
    if (!mag || len <= 0 || !s_mag) {
        return;
    }
    int max = s_n;   /* n/2 int16 = n bytes */
    if (len > max) {
        len = max;
    }
    memcpy(s_mag, mag, (size_t)len);
    s_mag_len = len;
}

void fmrb_fft_spx_log(const char *msg, int len)
{
    if (msg && len > 0) {
        FMRB_LOGI(TAG, "%.*s", len, msg);
    }
}

#else /* no Spinel FFT in this build */

int fmrb_fft_spinel_available(void) { return 0; }
int fmrb_fft_spinel_begin(int size) { (void)size; return -1; }
void fmrb_fft_spinel_end(void) { }

uint32_t fmrb_fft_spinel_run(const int16_t *in, int n, int iters, int16_t *mag_out)
{
    (void)in; (void)n; (void)iters; (void)mag_out;
    return 0;
}

#endif /* FMRB_FFT_SPINEL */
