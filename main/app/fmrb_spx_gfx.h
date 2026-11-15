/**
 * @file fmrb_spx_gfx.h
 * @brief Spinel FFI shim for the FmrbGfx operations not on the app boundary
 *        yet (video, sprites): an mrb-free C ABI wrapping the same graphics
 *        operations the mruby binding uses (picoruby-fmrb-app gfx.c).
 *
 * Spinel-generated C cannot take an mrb_state and cannot receive C structs
 * through its FFI, so every drawing primitive is exposed as a plain C function
 * whose first argument is the target canvas id (the Spinel FmrbGfx object keeps
 * its canvas id as a plain Integer instance variable instead of a boxed
 * mrb_gfx_data). See doc/spinel_aot/phase4.md T4-1 and the kernel precedent in
 * main/kernel/fmrb_spx.h.
 *
 * Return convention (mirrors fmrb_spx.h):
 * - Drawing commands return 0 on success, negative on error (a shim-local
 *   negative code). They never raise; the Ruby base class decides whether an
 *   error is fatal.
 * - Queries that yield a single scalar return that value when >= 0 and a
 *   negative code on error/absence.
 * - Structured results cross the boundary as fixed-layout little-endian byte
 *   buffers returned via :binstr (a real Spinel String read with getbyte); the
 *   byte length is published in sp_ffi_bin_len. An empty String means "nil".
 */
#ifndef FMRB_SPX_GFX_H
#define FMRB_SPX_GFX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared negative error codes (kept numerically identical to fmrb_spx.h). */
#ifndef FMRB_SPX_ERR
#define FMRB_SPX_ERR (-1)          /**< generic failure */
#define FMRB_SPX_ERR_RANGE (-2)    /**< argument out of range */
#define FMRB_SPX_ERR_CAP (-3)      /**< caller buffer too small */
#endif

/* The FmrbGfx methods on the app boundary are not here: their flat functions
   are generated from sig/ (fmrb_gen/fmrb_boundary.h, doc/app_boundary), and
   this file keeps what the boundary does not carry yet (video, sprites). */

/* --- motion-JPEG playback (Modern/P4 only) --- */
/**
 * @brief Play a file of concatenated JPEG frames into a canvas. Returns a
 *        4-byte :binstr record (width u16, height u16, LE) or an empty String
 *        (Ruby returns nil) when this backend cannot play it.
 */
#define FMRB_SPX_GFX_VIDEO_INFO_RECORD_SIZE 4
const char *fmrb_spx_gfx_video_open(int canvas_id, const char *path, int len,
                                    int x, int y, int fps, int loop);
/**
 * @brief Player state as a 9-byte :binstr record: state u8, frames shown u32,
 *        frames dropped u32 (LE). Empty String when the backend did not answer.
 */
#define FMRB_SPX_GFX_VIDEO_STATUS_RECORD_SIZE 9
const char *fmrb_spx_gfx_video_control(int action);
const char *fmrb_spx_gfx_video_status(void);

/* --- sprite API --- */
int fmrb_spx_gfx_create_sprite_image(int canvas_id, int width, int height,
                                     int trans_color, int use_trans);
int fmrb_spx_gfx_delete_sprite_image(int canvas_id, int image_id);
int fmrb_spx_gfx_load_sprite_image_bmp(int canvas_id, int image_id, const char *path, int len);
int fmrb_spx_gfx_set_sprite_image_target(int canvas_id, int image_id);
/** `frames` is a packed buffer of @p frame_count u16 LE image ids. */
int fmrb_spx_gfx_create_sprite_instance(int canvas_id, const char *frames, int frame_count,
                                        int x, int y, int z_order);
int fmrb_spx_gfx_delete_sprite_instance(int canvas_id, int instance_id);
int fmrb_spx_gfx_sprite_move(int canvas_id, int instance_id, int x, int y);
int fmrb_spx_gfx_sprite_visible(int canvas_id, int instance_id, int visible);
int fmrb_spx_gfx_sprite_frame(int canvas_id, int instance_id, int frame_index);
int fmrb_spx_gfx_delete_all_sprites(int canvas_id);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_SPX_GFX_H */
