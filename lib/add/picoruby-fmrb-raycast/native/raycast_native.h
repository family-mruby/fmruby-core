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
 * and the core built against it (trig tables included) is kept alive between
 * calls by --persistent-statics. Each upload bumps a generation counter, which
 * is how the Spinel entry learns to rebuild.
 *
 * Single owner: the instance and the I/O below are file-scope statics and the
 * instance is current on whichever task called begin(). One task only.
 */
#ifndef RAYCAST_NATIVE_H
#define RAYCAST_NATIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Largest map the receiver will hold, in cells. */
#define RAYCAST_MAP_MAX (64 * 64)

/** Is the Spinel raycaster compiled into this firmware? */
int raycast_available(void);

/** Microseconds from a monotonic clock -- the same one the Spinel entry times
 *  itself with, so the :ruby and :spinel numbers are comparable. */
uint32_t raycast_micros(void);

/**
 * Create the Spinel instance on the calling task.
 * @return 0 on success, negative on failure (no memory for the pool, or the
 *         runtime refusing to build an instance).
 */
int raycast_begin(void);

/**
 * Upload the world. Copied into the receiver and given a new generation, so
 * the next run() rebuilds the core against it.
 * @param cells  w*h bytes, one per cell
 * @return 0 on success, negative if the map is missing or larger than
 *         RAYCAST_MAP_MAX.
 */
int raycast_set_map(const uint8_t *cells, int w, int h);

/**
 * Cast a frame's worth of rays for a player at (px, py) facing pa degrees.
 * @param out_len  bytes written to the returned buffer
 * @param out_us   microseconds the Ruby spent casting, timed inside the entry
 * @return the packed depth buffer (six bytes a ray: int32 dist, wall, side),
 *         or NULL if the backend is not open or has no map.
 */
const char *raycast_run(int px, int py, int pa, int *out_len, uint32_t *out_us);

/** Tear the instance down and release its pool. */
void raycast_end(void);

#ifdef __cplusplus
}
#endif

#endif /* RAYCAST_NATIVE_H */
