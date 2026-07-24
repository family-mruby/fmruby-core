/**
 * @file fmrb_spx_gfx.h
 * @brief Spinel FFI shim for FmrbGfx: an mrb-free C ABI wrapping the same
 *        graphics operations the mruby FmrbGfx binding uses
 *        (lib/add/picoruby-fmrb-app/ports/esp32/gfx.c).
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
 *   byte length is published in sp_net_bin_len. An empty String means "nil".
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

/* --- basic drawing primitives (canvas_id, ints...) -> 0 ok / neg err --- */
int fmrb_spx_gfx_clear(int canvas_id, int color);
int fmrb_spx_gfx_set_pixel(int canvas_id, int x, int y, int color);
/** Flushes queued draws then reads one pixel. >= 0 is the RGB332 value; a
 *  negative code is a transport error (out-of-range reads return 0). */
int fmrb_spx_gfx_get_pixel(int canvas_id, int x, int y);
int fmrb_spx_gfx_draw_line(int canvas_id, int x1, int y1, int x2, int y2, int color);
int fmrb_spx_gfx_draw_rect(int canvas_id, int x, int y, int w, int h, int color);
int fmrb_spx_gfx_fill_rect(int canvas_id, int x, int y, int w, int h, int color);
/** mode: 0=ADD (saturating), 1=XOR. */
int fmrb_spx_gfx_blend_rect(int canvas_id, int x, int y, int w, int h, int color, int mode);
int fmrb_spx_gfx_draw_circle(int canvas_id, int x, int y, int r, int color);
int fmrb_spx_gfx_fill_circle(int canvas_id, int x, int y, int r, int color);
int fmrb_spx_gfx_draw_round_rect(int canvas_id, int x, int y, int w, int h, int r, int color);
int fmrb_spx_gfx_fill_round_rect(int canvas_id, int x, int y, int w, int h, int r, int color);
int fmrb_spx_gfx_draw_ellipse(int canvas_id, int x, int y, int rx, int ry, int color);
int fmrb_spx_gfx_fill_ellipse(int canvas_id, int x, int y, int rx, int ry, int color);
int fmrb_spx_gfx_draw_triangle(int canvas_id, int x0, int y0, int x1, int y1, int x2, int y2, int color);
int fmrb_spx_gfx_fill_triangle(int canvas_id, int x0, int y0, int x1, int y1, int x2, int y2, int color);
int fmrb_spx_gfx_draw_arc(int canvas_id, int x, int y, int r0, int r1, int angle0, int angle1, int color);
int fmrb_spx_gfx_fill_arc(int canvas_id, int x, int y, int r0, int r1, int angle0, int angle1, int color);

/* --- text --- */
int fmrb_spx_gfx_set_text_size(int canvas_id, int size);
/** family: 0=default, 1=ja. size ignored for :default; :ja accepts 8 or 12. */
int fmrb_spx_gfx_set_font(int canvas_id, int family, int size);
/**
 * @brief Draw text. `text`/`len` carry the UTF-8 bytes (:str + explicit length,
 *        so embedded NUL survives and no strlen is needed).
 * @param flags bit0 = bg given (opaque background), bit1 = hybrid glyph mode.
 */
int fmrb_spx_gfx_draw_text(int canvas_id, int x, int y, const char *text, int len,
                           int color, int bg_color, int flags);

/**
 * @brief Present the canvas. When @p explicit is 0 the canvas is presented at
 *        the current app window position (like FmrbGfx#present with no args);
 *        otherwise at (x, y).
 */
int fmrb_spx_gfx_present(int canvas_id, int x, int y, int explicit_pos);

/* --- CVBS/NTSC output control --- */
int fmrb_spx_gfx_set_output_level(int canvas_id, int level);
int fmrb_spx_gfx_set_chroma_level(int canvas_id, int level);

/**
 * @brief Set per-canvas composite regions. `packed` is a flat little-endian
 *        buffer of @p count records, each 14 bytes = 7 int16 fields in order
 *        src_x, src_y, dst_x, dst_y, w, h, use_transparent (0/1). An empty
 *        buffer (count 0) clears regions. Returns 0 (best-effort, like mruby).
 */
#define FMRB_SPX_GFX_REGION_RECORD_SIZE 14
int fmrb_spx_gfx_set_composite_regions(int canvas_id, const char *packed, int count);

/**
 * @brief Composite source viewport (hardware scroll register; P4/PPA backend
 *        only). view_w == 0 clears.
 */
int fmrb_spx_gfx_set_canvas_viewport(int canvas_id, int src_x, int src_y, int view_w, int view_h);

/* --- image API --- */
/** scale_x_fp8 / scale_y_fp8 are 8.8 fixed point (Ruby passes (scale*256)),
 *  keeping floats off the FFI boundary. */
int fmrb_spx_gfx_draw_image(int canvas_id, int image_id, int x, int y,
                            int scale_x_fp8, int scale_y_fp8);
int fmrb_spx_gfx_delete_image(int canvas_id, int image_id);
/**
 * @brief Create an image from a graphics-board file. Returns a 6-byte :binstr
 *        record (id u16, width u16, height u16, all LE) or an empty String
 *        (Ruby returns nil) when the file is missing/undecodable.
 */
#define FMRB_SPX_GFX_IMAGE_INFO_RECORD_SIZE 6
const char *fmrb_spx_gfx_create_image_from_file(int canvas_id, const char *path, int len);
/**
 * @brief Upload a 1bpp mask. `data`/`len` are the packed rows
 *        (((w+7)/8)*h bytes). Returns the mask id (> 0) or a negative code.
 */
int fmrb_spx_gfx_create_mask(int canvas_id, int width, int height, const char *data, int len);
int fmrb_spx_gfx_delete_mask(int canvas_id, int mask_id);
int fmrb_spx_gfx_draw_image_masked(int canvas_id, int image_id, int mask_id, int x, int y);
int fmrb_spx_gfx_draw_tile(int canvas_id, int image_id, int src_x, int src_y,
                           int w, int h, int dst_x, int dst_y);

/* --- file transfer --- */
/** Copy a local file to the graphics board FS. Returns 1 on success, negative
 *  on error (the mruby version raises; here the Ruby base decides). */
int fmrb_spx_gfx_transfer_file(const char *src, int slen, const char *dst, int dlen);
/**
 * @brief Query a graphics-board file. Returns the size (>= 0) when it exists,
 *        FMRB_SPX_ERR when it does not, or a smaller negative code on error.
 */
int fmrb_spx_gfx_file_status(const char *path, int len);

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
