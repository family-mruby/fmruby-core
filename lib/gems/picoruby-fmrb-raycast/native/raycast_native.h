/**
 * @file raycast_native.h
 * @brief Running the Spinel-compiled raycaster from an mruby app task.
 *
 * The demo behind doc/raycast_spinel/plan.md: the same Ruby
 * (mrblib/raycast_core.rb) runs on the mruby VM and as Spinel-compiled native
 * code, and the game flips between them while it draws, so the difference
 * shows up as a microsecond count under an unchanged picture. The raycaster is
 * fixed-point throughout, so what the two numbers differ by is the engine and
 * not the arithmetic -- unlike the FFT, where double on a single-precision FPU
 * dominated everything else.
 *
 * begin/end bracket the instance because creating it claims a memory pool;
 * doing that per frame would time the pool rather than the rays.
 *
 * The map is uploaded separately from the per-frame call: it changes rarely,
 * and the core built against it (trig tables included) stays alive in the
 * Spinel program between calls. Each upload builds a new core.
 *
 * Single owner: the instance and the I/O below are file-scope statics and the
 * instance is current on the task that called begin(). That app task owns it
 * until it calls end() or ends; begin() from any other task answers
 * RAYCAST_BUSY and the gem's Ruby runs that app on :ruby instead
 * (doc/spinel_multi_instance/report/g1.md).
 */
#ifndef RAYCAST_NATIVE_H
#define RAYCAST_NATIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** raycast_begin: another app task owns the instance. */
#define RAYCAST_BUSY (-5)

/** Largest map the receiver will hold, in cells. */
#define RAYCAST_MAP_MAX (64 * 64)

/** Is the Spinel raycaster compiled into this firmware? */
int raycast_available(void);

/** Microseconds from a monotonic clock -- the same one the Spinel cast is
 *  timed with, so the :ruby and :spinel numbers are comparable. */
uint32_t raycast_micros(void);

/**
 * Create the Spinel instance on the calling task, which then owns it.
 * @return 0 on success (or if the calling task already owns it),
 *         RAYCAST_BUSY if another task owns it, other negatives on failure
 *         (no memory for the pool, or the runtime refusing to build an
 *         instance).
 */
int raycast_begin(void);

/**
 * Log that the calling app was given another backend because the instance is
 * owned elsewhere: which app, which owner, which backend it runs on.
 */
void raycast_note_fallback(const char *backend);

/**
 * Upload the world: the Spinel program copies it and builds its core against
 * it. Needs an open instance (raycast_begin); a new instance needs the map
 * again.
 * @param cells  w*h bytes, one per cell
 * @return 0 on success, negative if the map is missing or larger than
 *         RAYCAST_MAP_MAX, the instance is not open, or the program rejected
 *         the map.
 */
int raycast_set_map(const uint8_t *cells, int w, int h);

/**
 * Cast a frame's worth of rays for a player at (px, py) facing pa degrees.
 * @param out_len  bytes written to the returned buffer
 * @param out_us   microseconds the cast took, timed around the entry call
 * @return the packed depth buffer (six bytes a ray: int32 dist, wall, side),
 *         valid until the next run() or end(); NULL if the backend is not
 *         open or has no map.
 */
const char *raycast_run(int px, int py, int pa, int *out_len, uint32_t *out_us);

/** Tear the instance down and release its pool. A no-op unless the calling
 *  task owns it. */
void raycast_end(void);

#ifdef __cplusplus
}
#endif

#endif /* RAYCAST_NATIVE_H */
