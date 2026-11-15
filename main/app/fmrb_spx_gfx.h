/**
 * @file fmrb_spx_gfx.h
 * @brief Spinel FFI shim for the FmrbGfx operations not on the app boundary
 *        yet (video, sprites): an mrb-free C ABI wrapping the same graphics
 *        operations the mruby binding uses (picoruby-fmrb-app gfx.c).
 *
 * Spinel-generated C cannot take an mrb_state and cannot receive C structs
 * through its FFI, so every drawing primitive is exposed as a plain C function
 * whose first argument is the FmrbGfx object's drawer number (fmrb_gfx_drawer.h:
 * the canvas, the drawing origin, whether it draws into a sprite image; the
 * Spinel FmrbGfx keeps the number as a plain Integer instance variable, as the
 * boundary's functions take it). See doc/spinel_aot/phase4.md T4-1 and the
 * kernel precedent in main/kernel/fmrb_spx.h.
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

/* --- the drawer (FmrbGfx.new, _set_origin, destroy) --- */
/** A drawer of the app on a canvas: its number, or negative when none is left. */
int fmrb_spx_gfx_drawer_open(int canvas_id, int width, int height);
int fmrb_spx_gfx_drawer_set_origin(int drawer, int x, int y, int w, int h);
int fmrb_spx_gfx_drawer_close(int drawer);

/* --- motion-JPEG playback (Modern/P4 only) --- */
/**
 * @brief Play a file of concatenated JPEG frames into a canvas. Returns a
 *        4-byte :binstr record (width u16, height u16, LE) or an empty String
 *        (Ruby returns nil) when this backend cannot play it.
 */
#define FMRB_SPX_GFX_VIDEO_INFO_RECORD_SIZE 4
const char *fmrb_spx_gfx_video_open(int drawer, const char *path, int len,
                                    int x, int y, int fps, int loop);
/**
 * @brief Player state as a 9-byte :binstr record: state u8, frames shown u32,
 *        frames dropped u32 (LE). Empty String when the backend did not answer.
 */
#define FMRB_SPX_GFX_VIDEO_STATUS_RECORD_SIZE 9
const char *fmrb_spx_gfx_video_control(int action);
const char *fmrb_spx_gfx_video_status(void);

/* --- sprite API (sprite coordinates from the drawer's origin) --- */
int fmrb_spx_gfx_create_sprite_image(int drawer, int width, int height,
                                     int trans_color, int use_trans);
int fmrb_spx_gfx_delete_sprite_image(int drawer, int image_id);
int fmrb_spx_gfx_load_sprite_image_bmp(int drawer, int image_id, const char *path, int len);
int fmrb_spx_gfx_set_sprite_image_target(int drawer, int image_id);
/** `frames` is a packed buffer of @p frame_count u16 LE image ids. */
int fmrb_spx_gfx_create_sprite_instance(int drawer, const char *frames, int frame_count,
                                        int x, int y, int z_order);
int fmrb_spx_gfx_delete_sprite_instance(int drawer, int instance_id);
int fmrb_spx_gfx_sprite_move(int drawer, int instance_id, int x, int y);
int fmrb_spx_gfx_sprite_visible(int drawer, int instance_id, int visible);
int fmrb_spx_gfx_sprite_frame(int drawer, int instance_id, int frame_index);
int fmrb_spx_gfx_delete_all_sprites(int drawer);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_SPX_GFX_H */
