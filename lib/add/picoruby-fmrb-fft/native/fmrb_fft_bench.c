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

/* Placement for the cached-table pointers and size scalars below. Each is read
   once per transform call (to fetch a buffer base or check the cached size),
   never per sample -- the hot memory is the buffers they point at, which
   fmrb_sys_malloc already puts in PSRAM. So these too go to PSRAM on ESP32 to
   spare internal DRAM; on the Linux build it is plain BSS. Same shape as
   FMRB_DBG_BSS_ATTR / FMRB_SPX_BSS_ATTR. */
#ifdef CONFIG_IDF_TARGET_LINUX
#define FFT_BSS_ATTR
#else
#include "esp_attr.h"
#define FFT_BSS_ATTR EXT_RAM_BSS_ATTR
#endif

/* Cached per size: the tables the transform reads and the scratch it works in.
   n_cached == 0 means nothing is allocated. */
FFT_BSS_ATTR static int    s_n;
FFT_BSS_ATTR static float *s_window;   /* n     Hann coefficients */
FFT_BSS_ATTR static float *s_cos;      /* n/2   twiddle, cos(-2 pi k / n) */
FFT_BSS_ATTR static float *s_sin;      /* n/2   twiddle, sin(-2 pi k / n) */
FFT_BSS_ATTR static float *s_re;       /* n     working real part */
FFT_BSS_ATTR static float *s_im;       /* n     working imaginary part */
FFT_BSS_ATTR static int   *s_rev;      /* n     bit-reversal permutation */

/* The same six, in double, for fmrb_fft_c_f64. Kept separate rather than
   templated so the float path a caller reads is the float path that runs. */
FFT_BSS_ATTR static int     d_n;
FFT_BSS_ATTR static double *d_window;
FFT_BSS_ATTR static double *d_cos;
FFT_BSS_ATTR static double *d_sin;
FFT_BSS_ATTR static double *d_re;
FFT_BSS_ATTR static double *d_im;
FFT_BSS_ATTR static int    *d_rev;

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

static void fft_free_tables_f64(void)
{
    fmrb_sys_free(d_window); d_window = NULL;
    fmrb_sys_free(d_cos);    d_cos = NULL;
    fmrb_sys_free(d_sin);    d_sin = NULL;
    fmrb_sys_free(d_re);     d_re = NULL;
    fmrb_sys_free(d_im);     d_im = NULL;
    fmrb_sys_free(d_rev);    d_rev = NULL;
    d_n = 0;
}

/* Defined with the Q15 transform further down; released here with the rest. */
static void fft_free_tables_q15(void);

void fmrb_fft_release(void)
{
    fft_free_tables();
    fft_free_tables_f64();
    fft_free_tables_q15();
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

/* ---- The same transform in double ----------------------------------------
 *
 * Line for line the float version above, with every real number widened to
 * double and cosf/sinf/sqrtf replaced by cos/sin/sqrt. It exists to isolate one
 * variable in the engine comparison (doc/mic_spectrum/impl_plan_spinel_perf.md
 * E1): mrb_float -- what fft_core.rb computes in, under mruby and under Spinel
 * alike -- is a double, while this file's baseline is float32. On a chip whose
 * FPU is single precision only (the P4 is RV32IMAFC: no "D"), that difference
 * is not a rounding detail, it is hardware arithmetic versus a software
 * emulation of it. Timing c against c64 prices the soft-float tax; timing
 * spinel against c64 is then the Ruby overhead alone, at equal precision.
 *
 * It is a duplicate on purpose. A fft_real_t typedef would halve the lines and
 * make both paths hostage to one set of macros, and the whole value of these
 * numbers is that nobody has to wonder whether the two builds really did the
 * same arithmetic.
 */

static int fft_prepare_f64(int n)
{
    if (d_n == n) {
        return 1;
    }
    fft_free_tables_f64();

    d_window = (double *)fmrb_sys_malloc(sizeof(double) * (size_t)n);
    d_cos    = (double *)fmrb_sys_malloc(sizeof(double) * (size_t)(n / 2));
    d_sin    = (double *)fmrb_sys_malloc(sizeof(double) * (size_t)(n / 2));
    d_re     = (double *)fmrb_sys_malloc(sizeof(double) * (size_t)n);
    d_im     = (double *)fmrb_sys_malloc(sizeof(double) * (size_t)n);
    d_rev    = (int *)fmrb_sys_malloc(sizeof(int) * (size_t)n);
    if (!d_window || !d_cos || !d_sin || !d_re || !d_im || !d_rev) {
        fft_free_tables_f64();
        return 0;
    }

    for (int i = 0; i < n; i++) {
        d_window[i] = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)n);
    }
    for (int k = 0; k < n / 2; k++) {
        double a = -2.0 * M_PI * (double)k / (double)n;
        d_cos[k] = cos(a);
        d_sin[k] = sin(a);
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
        d_rev[i] = r;
    }

    d_n = n;
    return 1;
}

static void fft_once_f64(const int16_t *in, int n)
{
    for (int i = 0; i < n; i++) {
        d_re[d_rev[i]] = (double)in[i] * d_window[i];
        d_im[d_rev[i]] = 0.0;
    }

    for (int len = 2; len <= n; len <<= 1) {
        int half = len >> 1;
        int step = n / len;
        for (int i = 0; i < n; i += len) {
            int k = 0;
            for (int j = 0; j < half; j++) {
                double wr = d_cos[k];
                double wi = d_sin[k];
                int a = i + j;
                int b = a + half;
                double tr = d_re[b] * wr - d_im[b] * wi;
                double ti = d_re[b] * wi + d_im[b] * wr;
                d_re[b] = d_re[a] - tr;
                d_im[b] = d_im[a] - ti;
                d_re[a] += tr;
                d_im[a] += ti;
                k += step;
            }
        }
    }
}

static void fft_magnitudes_f64(int n, int16_t *mag_out)
{
    if (!mag_out) {
        return;
    }
    double scale = 2.0 / (double)n;
    for (int i = 0; i < n / 2; i++) {
        double m = sqrt(d_re[i] * d_re[i] + d_im[i] * d_im[i]) * scale;
        if (m > 32767.0) {
            m = 32767.0;
        }
        mag_out[i] = (int16_t)(m + 0.5);
    }
}

uint32_t fmrb_fft_c_f64(const int16_t *in, int n, int iters, int16_t *mag_out)
{
    if (!in || iters < 1 || !fft_is_pow2(n) || !fft_prepare_f64(n)) {
        return 0;
    }

    uint32_t t0 = fmrb_fft_micros();
    for (int i = 0; i < iters; i++) {
        fft_once_f64(in, n);
    }
    uint32_t us = fmrb_fft_micros() - t0;

    fft_magnitudes_f64(n, mag_out);
    return us;
}

/* ---- The same transform in Q15 fixed point --------------------------------
 *
 * Mirrors mrblib/fft_core_q15.rb line for line, the way the float version
 * mirrors fft_core.rb. The header of that file carries the argument for why
 * int32 is enough everywhere and why the magnitudes come out doubled instead
 * of scaled by 2/n; it is not repeated here, because the point of having two
 * files that read alike is that one explanation covers both.
 *
 * Why it exists: the two Ruby engines compute in double, and a double on a
 * single-precision FPU is a software emulation. Integers are not, on any of
 * the engines -- and on Spinel an mrb_int is pointer-width, so on the device
 * these are 32-bit machine integers. This is the arithmetic an embedded person
 * would have reached for first (doc/mic_spectrum/impl_plan_spinel_perf.md, E4).
 */

FFT_BSS_ATTR static int      q_n;
FFT_BSS_ATTR static int32_t *q_window;
FFT_BSS_ATTR static int32_t *q_cos;
FFT_BSS_ATTR static int32_t *q_sin;
FFT_BSS_ATTR static int32_t *q_re;
FFT_BSS_ATTR static int32_t *q_im;
FFT_BSS_ATTR static int     *q_rev;

static void fft_free_tables_q15(void)
{
    fmrb_sys_free(q_window); q_window = NULL;
    fmrb_sys_free(q_cos);    q_cos = NULL;
    fmrb_sys_free(q_sin);    q_sin = NULL;
    fmrb_sys_free(q_re);     q_re = NULL;
    fmrb_sys_free(q_im);     q_im = NULL;
    fmrb_sys_free(q_rev);    q_rev = NULL;
    q_n = 0;
}

static int fft_prepare_q15(int n)
{
    if (q_n == n) {
        return 1;
    }
    fft_free_tables_q15();

    q_window = (int32_t *)fmrb_sys_malloc(sizeof(int32_t) * (size_t)n);
    q_cos    = (int32_t *)fmrb_sys_malloc(sizeof(int32_t) * (size_t)(n / 2));
    q_sin    = (int32_t *)fmrb_sys_malloc(sizeof(int32_t) * (size_t)(n / 2));
    q_re     = (int32_t *)fmrb_sys_malloc(sizeof(int32_t) * (size_t)n);
    q_im     = (int32_t *)fmrb_sys_malloc(sizeof(int32_t) * (size_t)n);
    q_rev    = (int *)fmrb_sys_malloc(sizeof(int) * (size_t)n);
    if (!q_window || !q_cos || !q_sin || !q_re || !q_im || !q_rev) {
        fft_free_tables_q15();
        return 0;
    }

    /* Built in double and truncated with the same expression the Ruby uses, so
       both engines start from the same table down to the last count. */
    for (int i = 0; i < n; i++) {
        double w = (0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)n)) * 32768.0;
        if (w > 32767.0) {
            w = 32767.0;
        }
        q_window[i] = (int32_t)w;
    }
    for (int k = 0; k < n / 2; k++) {
        double a = -2.0 * M_PI * (double)k / (double)n;
        double c = cos(a) * 32768.0;
        double s = sin(a) * 32768.0;
        if (c > 32767.0)  { c = 32767.0; }
        if (c < -32767.0) { c = -32767.0; }
        if (s > 32767.0)  { s = 32767.0; }
        if (s < -32767.0) { s = -32767.0; }
        q_cos[k] = (int32_t)c;
        q_sin[k] = (int32_t)s;
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
        q_rev[i] = r;
    }

    q_n = n;
    return 1;
}

static void fft_once_q15(const int16_t *in, int n)
{
    for (int i = 0; i < n; i++) {
        q_re[q_rev[i]] = ((int32_t)in[i] * q_window[i]) >> 15;
        q_im[q_rev[i]] = 0;
    }

    for (int len = 2; len <= n; len <<= 1) {
        int half = len >> 1;
        int step = n / len;
        for (int i = 0; i < n; i += len) {
            int k = 0;
            for (int j = 0; j < half; j++) {
                int32_t wr = q_cos[k];
                int32_t wi = q_sin[k];
                int a = i + j;
                int b = a + half;
                int32_t rb = q_re[b];
                int32_t ib = q_im[b];
                int32_t tr = (rb * wr - ib * wi) >> 15;
                int32_t ti = (rb * wi + ib * wr) >> 15;
                int32_t ra = q_re[a];
                int32_t ia = q_im[a];
                q_re[b] = (ra - tr) >> 1;
                q_im[b] = (ia - ti) >> 1;
                q_re[a] = (ra + tr) >> 1;
                q_im[a] = (ia + ti) >> 1;
                k += step;
            }
        }
    }
}

static int32_t fft_isqrt(int32_t v)
{
    if (v <= 0) {
        return 0;
    }
    int32_t x = v;
    int32_t y = (x + 1) >> 1;
    while (y < x) {
        x = y;
        y = (x + v / x) >> 1;
    }
    return x;
}

static void fft_magnitudes_q15(int n, int16_t *mag_out)
{
    if (!mag_out) {
        return;
    }
    for (int i = 0; i < n / 2; i++) {
        int32_t r = q_re[i];
        int32_t m = q_im[i];
        int32_t v = 2 * fft_isqrt(r * r + m * m);
        if (v > 32767) {
            v = 32767;
        }
        mag_out[i] = (int16_t)v;
    }
}

uint32_t fmrb_fft_c_q15(const int16_t *in, int n, int iters, int16_t *mag_out)
{
    if (!in || iters < 1 || !fft_is_pow2(n) || !fft_prepare_q15(n)) {
        return 0;
    }

    uint32_t t0 = fmrb_fft_micros();
    for (int i = 0; i < iters; i++) {
        fft_once_q15(in, n);
    }
    uint32_t us = fmrb_fft_micros() - t0;

    fft_magnitudes_q15(n, mag_out);
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
FFT_BSS_ATTR static int   s_dsp_n;
FFT_BSS_ATTR static float *s_dsp_buf;   /* 2 * n interleaved */

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
