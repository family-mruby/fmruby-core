/**
 * @file fmrb_fft_spinel.h
 * @brief Running the Spinel-compiled FFT from a task that is not a Spinel task.
 *
 * The other three backends are ordinary function calls. This one is the new
 * pattern the comparison exists to try (doc/mic_spectrum/impl_plan.md Stage 2):
 * an mruby app task creates a Spinel runtime instance of its own, calls the
 * AOT-compiled entry point as if it were a library, and tears the instance
 * down again -- so one build can time the same Ruby on both engines instead of
 * needing two firmwares.
 *
 * begin/end bracket the instance because creating it claims a memory pool;
 * doing that per transform would time the pool rather than the FFT.
 *
 * Every function is safe to call on a build without Spinel: available() says
 * 0, begin() fails, run() returns 0 microseconds.
 */
#ifndef FMRB_FFT_SPINEL_H
#define FMRB_FFT_SPINEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Is the Spinel FFT compiled into this firmware? */
int fmrb_fft_spinel_available(void);

/**
 * Create the Spinel instance on the calling task and prepare a transform of
 * `size` points.
 * @return 0 on success, negative on failure (no Spinel in this build, no
 *         memory for the pool, or the entry point reporting an error).
 */
int fmrb_fft_spinel_begin(int size);

/**
 * Run the Spinel FFT `iters` times, timed inside the Spinel program exactly as
 * the C backend times itself.
 * @return total microseconds, or 0 if the backend is not open.
 */
uint32_t fmrb_fft_spinel_run(const int16_t *in, int n, int iters, int16_t *mag_out);

/**
 * The same, running the Q15 fixed-point core (mrblib/fft_core_q15.rb) instead
 * of the double one. Same instance, same pool -- both cores are compiled into
 * the one Spinel program and selected per call.
 */
uint32_t fmrb_fft_spinel_run_q15(const int16_t *in, int n, int iters, int16_t *mag_out);

/**
 * Microseconds the last run() spent in the entry as a whole, as opposed to the
 * transform the Ruby timed for itself.
 *
 * The gap between the two is not overhead in the usual sense: a Spinel entry
 * resets its class-level statics on every invocation, so the program builds
 * its window and twiddle tables again each call, while the C backend keeps
 * its own across calls. A benchmark that puts the repetition inside the entry
 * never sees this; a caller asking for one transform per frame pays it every
 * frame. Worth reading before quoting a per-frame cost.
 */
uint32_t fmrb_fft_spinel_last_total_us(void);

/** Tear the instance down and release its pool. */
void fmrb_fft_spinel_end(void);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_FFT_SPINEL_H */
