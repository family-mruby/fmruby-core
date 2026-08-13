/*
 * FftNative -- the thin mruby side of the FFT comparison (mrblib/fmrb-fft.rb).
 *
 * Nothing is computed here. Every entry point converts an int16 byte String
 * into an aligned buffer, calls one of the plain-C functions in
 * main/kernel/fmrb_fft_bench.c (or the Spinel shim next to it), and hands back
 * [microseconds, magnitude bytes]. Keeping the conversion out of the timed
 * region is the reason the C functions take a buffer and a count rather than
 * an mruby value: the four engines have to be timed over the same work.
 *
 * The copy is not paranoia about speed -- it is alignment. A String's bytes
 * start wherever the allocator put them, and a const int16_t* into an odd
 * address is a fault on Xtensa.
 */
#include <stdint.h>
#include <string.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/string.h>

#include "fmrb_fft_bench.h"
#include "fmrb_fft_spinel.h"

/* Copy the samples out of the String into an aligned int16 buffer, checking
 * that there are as many as the caller claims. Returns NULL after raising. */
static int16_t *fft_take_samples(mrb_state *mrb, mrb_value str, mrb_int n)
{
    if (n < 64 || n > FMRB_FFT_MAX_N || (n & (n - 1)) != 0) {
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "FFT size must be a power of two in 64..%d",
                   (int)FMRB_FFT_MAX_N);
    }
    if (RSTRING_LEN(str) < n * 2) {
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "need %d int16 samples, got %d bytes",
                   (int)n, (int)RSTRING_LEN(str));
    }
    int16_t *buf = (int16_t *)mrb_malloc(mrb, sizeof(int16_t) * (size_t)n);
    memcpy(buf, RSTRING_PTR(str), sizeof(int16_t) * (size_t)n);
    return buf;
}

/* [microseconds, magnitudes] for one of the C-side runners. */
typedef uint32_t (*fft_runner_t)(const int16_t *, int, int, int16_t *);

static mrb_value fft_run_with(mrb_state *mrb, fft_runner_t runner)
{
    mrb_value str;
    mrb_int n, iters;
    mrb_get_args(mrb, "Sii", &str, &n, &iters);
    if (iters < 1) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "iters must be >= 1");
    }

    int16_t *in = fft_take_samples(mrb, str, n);
    int16_t *mag = (int16_t *)mrb_malloc(mrb, sizeof(int16_t) * (size_t)(n / 2));

    uint32_t us = runner(in, (int)n, (int)iters, mag);

    mrb_value out = mrb_ary_new_capa(mrb, 2);
    mrb_ary_push(mrb, out, mrb_fixnum_value((mrb_int)us));
    mrb_ary_push(mrb, out, mrb_str_new(mrb, (const char *)mag,
                                       sizeof(int16_t) * (size_t)(n / 2)));
    mrb_free(mrb, mag);
    mrb_free(mrb, in);
    return out;
}

static mrb_value mrb_fft_micros(mrb_state *mrb, mrb_value self)
{
    (void)self;
    (void)mrb;
    return mrb_fixnum_value((mrb_int)fmrb_fft_micros());
}

static mrb_value mrb_fft_c_run(mrb_state *mrb, mrb_value self)
{
    (void)self;
    return fft_run_with(mrb, fmrb_fft_c);
}

static mrb_value mrb_fft_dsp_run(mrb_state *mrb, mrb_value self)
{
    (void)self;
    return fft_run_with(mrb, fmrb_fft_dsp);
}

static mrb_value mrb_fft_dsp_available(mrb_state *mrb, mrb_value self)
{
    (void)self;
    (void)mrb;
    return mrb_bool_value(fmrb_fft_dsp_available() != 0);
}

static mrb_value mrb_fft_release(mrb_state *mrb, mrb_value self)
{
    (void)self;
    (void)mrb;
    fmrb_fft_release();
    return mrb_nil_value();
}

/* ---- Spinel backend ------------------------------------------------------
 *
 * The Spinel instance lives on the calling task for as long as the backend is
 * open, so these are begin / run / end rather than one call: creating it
 * claims a memory pool, and doing that per transform would measure the pool
 * rather than the FFT.
 */

static mrb_value mrb_fft_spinel_available(mrb_state *mrb, mrb_value self)
{
    (void)self;
    (void)mrb;
    return mrb_bool_value(fmrb_fft_spinel_available() != 0);
}

static mrb_value mrb_fft_spinel_begin(mrb_state *mrb, mrb_value self)
{
    (void)self;
    mrb_int size;
    mrb_get_args(mrb, "i", &size);
    return mrb_fixnum_value(fmrb_fft_spinel_begin((int)size));
}

static mrb_value mrb_fft_spinel_run(mrb_state *mrb, mrb_value self)
{
    (void)self;
    return fft_run_with(mrb, fmrb_fft_spinel_run);
}

static mrb_value mrb_fft_spinel_end(mrb_state *mrb, mrb_value self)
{
    (void)self;
    (void)mrb;
    fmrb_fft_spinel_end();
    return mrb_nil_value();
}

void mrb_fmrb_fft_init(mrb_state *mrb)
{
    struct RClass *m = mrb_define_module(mrb, "FftNative");

    mrb_define_module_function(mrb, m, "micros", mrb_fft_micros, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, m, "c_run", mrb_fft_c_run, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, m, "dsp_run", mrb_fft_dsp_run, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, m, "dsp_available?", mrb_fft_dsp_available, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, m, "release", mrb_fft_release, MRB_ARGS_NONE());

    mrb_define_module_function(mrb, m, "spinel_available?", mrb_fft_spinel_available, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, m, "spinel_begin", mrb_fft_spinel_begin, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, m, "spinel_run", mrb_fft_spinel_run, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, m, "spinel_end", mrb_fft_spinel_end, MRB_ARGS_NONE());
}

void mrb_fmrb_fft_final(mrb_state *mrb)
{
    (void)mrb;
    fmrb_fft_release();
}
