/*
 * The C side of the four-engine FFT comparison (doc/mic_spectrum/plan.md).
 *
 * Written to be the plain, obvious radix-2 -- the one a person writes from the
 * textbook -- because its job is to be the fair baseline for the same
 * algorithm written in Ruby (fft_core.rb, run by mruby and by Spinel). Where
 * this file and that one differ in shape, the comparison is measuring the
 * difference in shape rather than the difference in engines, so they are kept
 * line-for-line alike: same window, same bit-reversal, same twiddle table,
 * same scaling.
 *
 * What is deliberately NOT here: SIMD, split-radix, real-input packing. The
 * hand-optimised ceiling is esp-dsp's dsps_fft2r, which is the fourth engine.
 *
 * Work buffers are allocated on first use and cached (fmrb_fft_release frees
 * them) rather than living in .bss: a 1024-point transform needs ~12KB of
 * float, and internal RAM on the S3 is not something to spend on a benchmark
 * that may never run (doc/internal_ram_budget.md).
 */

#include <math.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "fmrb_fft_bench.h"
#include "fmrb_mem.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4) || defined(CONFIG_IDF_TARGET_ESP32S3)
/* esp-dsp is added to main/idf_component.yml for the device builds; the Linux
   simulator has no such component, and no assembler kernel to compare against
   anyway. */
#if __has_include("esp_dsp.h")
#include "esp_dsp.h"
#define FMRB_FFT_HAVE_ESP_DSP 1
#endif
#endif

/* Cached per size: the tables the transform reads and the scratch it works in.
   n_cached == 0 means nothing is allocated. */
static int    s_n;
static float *s_window;   /* n     Hann coefficients */
static float *s_cos;      /* n/2   twiddle, cos(-2 pi k / n) */
static float *s_sin;      /* n/2   twiddle, sin(-2 pi k / n) */
static float *s_re;       /* n     working real part */
static float *s_im;       /* n     working imaginary part */
static int   *s_rev;      /* n     bit-reversal permutation */

uint32_t fmrb_fft_micros(void)
{
    /* One clock for every backend, so the four numbers are comparable. The
       IDF provides clock_gettime on the device as well, so this is the same
       source that fmrb_spx_board_micros uses -- that one is only compiled into
       Spinel builds, which is why it is not reused here. */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u);
}

static int fft_is_pow2(int n)
{
    return n >= 64 && n <= FMRB_FFT_MAX_N && (n & (n - 1)) == 0;
}

static void fft_free_tables(void)
{
    fmrb_sys_free(s_window); s_window = NULL;
    fmrb_sys_free(s_cos);    s_cos = NULL;
    fmrb_sys_free(s_sin);    s_sin = NULL;
    fmrb_sys_free(s_re);     s_re = NULL;
    fmrb_sys_free(s_im);     s_im = NULL;
    fmrb_sys_free(s_rev);    s_rev = NULL;
    s_n = 0;
}

void fmrb_fft_release(void)
{
    fft_free_tables();
}

/* Build the window, the twiddle table and the bit-reversal permutation for n.
   Everything the inner loops read is computed here exactly once: a Math.sin in
   the inner loop would turn the benchmark into a benchmark of sin(). */
static int fft_prepare(int n)
{
    if (s_n == n) {
        return 1;
    }
    fft_free_tables();

    s_window = (float *)fmrb_sys_malloc(sizeof(float) * (size_t)n);
    s_cos    = (float *)fmrb_sys_malloc(sizeof(float) * (size_t)(n / 2));
    s_sin    = (float *)fmrb_sys_malloc(sizeof(float) * (size_t)(n / 2));
    s_re     = (float *)fmrb_sys_malloc(sizeof(float) * (size_t)n);
    s_im     = (float *)fmrb_sys_malloc(sizeof(float) * (size_t)n);
    s_rev    = (int *)fmrb_sys_malloc(sizeof(int) * (size_t)n);
    if (!s_window || !s_cos || !s_sin || !s_re || !s_im || !s_rev) {
        fft_free_tables();
        return 0;
    }

    for (int i = 0; i < n; i++) {
        s_window[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)i / (float)n);
    }
    for (int k = 0; k < n / 2; k++) {
        float a = -2.0f * (float)M_PI * (float)k / (float)n;
        s_cos[k] = cosf(a);
        s_sin[k] = sinf(a);
    }

    int bits = 0;
    while ((1 << bits) < n) {
        bits++;
    }
    for (int i = 0; i < n; i++) {
        int r = 0;
        int v = i;
        for (int b = 0; b < bits; b++) {
            r = (r << 1) | (v & 1);
            v >>= 1;
        }
        s_rev[i] = r;
    }

    s_n = n;
    return 1;
}

/* One transform over the prepared tables. Input is windowed on the way in, so
   the caller's samples are never modified. */
static void fft_once(const int16_t *in, int n)
{
    /* Window and bit-reverse in one pass: the sample that belongs at index i
       is the one at the reversed position. */
    for (int i = 0; i < n; i++) {
        s_re[s_rev[i]] = (float)in[i] * s_window[i];
        s_im[s_rev[i]] = 0.0f;
    }

    for (int len = 2; len <= n; len <<= 1) {
        int half = len >> 1;
        int step = n / len;
        for (int i = 0; i < n; i += len) {
            int k = 0;
            for (int j = 0; j < half; j++) {
                float wr = s_cos[k];
                float wi = s_sin[k];
                int a = i + j;
                int b = a + half;
                float tr = s_re[b] * wr - s_im[b] * wi;
                float ti = s_re[b] * wi + s_im[b] * wr;
                s_re[b] = s_re[a] - tr;
                s_im[b] = s_im[a] - ti;
                s_re[a] += tr;
                s_im[a] += ti;
                k += step;
            }
        }
    }
}

/* n/2 magnitudes, scaled the way fft_core.rb scales them: 2/n, clamped into
   int16 so the three engines can be compared bin by bin. */
static void fft_magnitudes(int n, int16_t *mag_out)
{
    if (!mag_out) {
        return;
    }
    float scale = 2.0f / (float)n;
    for (int i = 0; i < n / 2; i++) {
        float m = sqrtf(s_re[i] * s_re[i] + s_im[i] * s_im[i]) * scale;
        if (m > 32767.0f) {
            m = 32767.0f;
        }
        mag_out[i] = (int16_t)(m + 0.5f);
    }
}

uint32_t fmrb_fft_c(const int16_t *in, int n, int iters, int16_t *mag_out)
{
    if (!in || iters < 1 || !fft_is_pow2(n) || !fft_prepare(n)) {
        return 0;
    }

    uint32_t t0 = fmrb_fft_micros();
    for (int i = 0; i < iters; i++) {
        fft_once(in, n);
    }
    uint32_t us = fmrb_fft_micros() - t0;

    fft_magnitudes(n, mag_out);
    return us;
}

int fmrb_fft_dsp_available(void)
{
#ifdef FMRB_FFT_HAVE_ESP_DSP
    return 1;
#else
    return 0;
#endif
}

#ifdef FMRB_FFT_HAVE_ESP_DSP

/* esp-dsp works on one interleaved re/im array and wants its own twiddle
   table, initialised once for the largest size we will ask for. */
static int   s_dsp_n;
static float *s_dsp_buf;   /* 2 * n interleaved */

static int fft_dsp_prepare(int n)
{
    if (s_dsp_n == n) {
        return 1;
    }
    if (s_dsp_buf) {
        fmrb_sys_free(s_dsp_buf);
        s_dsp_buf = NULL;
        s_dsp_n = 0;
    }
    if (dsps_fft2r_init_fc32(NULL, n) != ESP_OK) {
        return 0;
    }
    s_dsp_buf = (float *)fmrb_sys_malloc(sizeof(float) * 2u * (size_t)n);
    if (!s_dsp_buf) {
        return 0;
    }
    s_dsp_n = n;
    return 1;
}

uint32_t fmrb_fft_dsp(const int16_t *in, int n, int iters, int16_t *mag_out)
{
    if (!in || iters < 1 || !fft_is_pow2(n) || !fft_prepare(n) || !fft_dsp_prepare(n)) {
        return 0;
    }

    uint32_t t0 = fmrb_fft_micros();
    for (int it = 0; it < iters; it++) {
        /* Same window as everyone else, applied per iteration so the measured
           work is the same work. */
        for (int i = 0; i < n; i++) {
            s_dsp_buf[2 * i]     = (float)in[i] * s_window[i];
            s_dsp_buf[2 * i + 1] = 0.0f;
        }
        dsps_fft2r_fc32(s_dsp_buf, n);
        dsps_bit_rev_fc32(s_dsp_buf, n);
    }
    uint32_t us = fmrb_fft_micros() - t0;

    /* Copy into the shared work buffers so the magnitudes come out of one
       function -- identical rounding for every backend. */
    for (int i = 0; i < n; i++) {
        s_re[i] = s_dsp_buf[2 * i];
        s_im[i] = s_dsp_buf[2 * i + 1];
    }
    fft_magnitudes(n, mag_out);
    return us;
}

#else /* no esp-dsp in this build (the Linux simulator) */

uint32_t fmrb_fft_dsp(const int16_t *in, int n, int iters, int16_t *mag_out)
{
    (void)in; (void)n; (void)iters; (void)mag_out;
    return 0;
}

#endif /* FMRB_FFT_HAVE_ESP_DSP */
