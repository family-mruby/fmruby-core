/**
 * @file fmrb_fft_bench.h
 * @brief One radix-2 FFT in C, shared by every caller that needs one.
 *
 * The point of this file is the comparison in doc/mic_spectrum/plan.md: the
 * same transform, written the same way, run by four engines (C here, mruby and
 * Spinel from fft_core.rb, esp-dsp below) so the numbers say something about
 * the engines rather than about the algorithm. Everything a caller could get
 * wrong -- window, twiddle table, scaling -- is therefore fixed here and
 * mirrored exactly in the Ruby.
 *
 * The ABI is mrb-free on purpose: the mruby binding, the Spinel FFI shim and
 * (later) the microphone task all reach the same two functions.
 *
 * Shape of the transform, identical in all implementations:
 *   - input  : n int16 samples (n a power of two, 64..FMRB_FFT_MAX_N)
 *   - window : Hann, precomputed
 *   - core   : in-place radix-2 Cooley-Tukey, decimation in time,
 *              bit-reversal first, twiddles from a precomputed table
 *   - output : n/2 magnitudes as int16, sqrt(re^2+im^2) scaled by 2/n
 */
#ifndef FMRB_FFT_BENCH_H
#define FMRB_FFT_BENCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Largest transform the work buffers are sized for. */
#define FMRB_FFT_MAX_N 1024

/** Microseconds from a monotonic clock. esp_timer on the device,
 *  CLOCK_MONOTONIC on the Linux build -- one function so every backend is
 *  timed by the same clock. */
uint32_t fmrb_fft_micros(void);

/**
 * Run the plain C FFT `iters` times over the same input.
 *
 * @param in       n int16 samples
 * @param n        transform size (power of two, 64..FMRB_FFT_MAX_N)
 * @param iters    how many times to transform (>= 1)
 * @param mag_out  n/2 int16 magnitudes from the last iteration; may be NULL
 * @return         total microseconds for the `iters` transforms, 0 on a bad
 *                 argument or an allocation failure
 */
uint32_t fmrb_fft_c(const int16_t *in, int n, int iters, int16_t *mag_out);

/**
 * The same, through esp-dsp's assembler-optimised radix-2 (the ceiling of the
 * four-way comparison). Returns 0 on builds without esp-dsp (the Linux
 * simulator), where the caller should report the backend as unavailable.
 */
uint32_t fmrb_fft_dsp(const int16_t *in, int n, int iters, int16_t *mag_out);

/** Is fmrb_fft_dsp backed by a real implementation in this build? */
int fmrb_fft_dsp_available(void);

/** Release the cached work buffers (twiddle table, window, scratch). Safe to
 *  call at any time; the next transform allocates again. */
void fmrb_fft_release(void);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_FFT_BENCH_H */
