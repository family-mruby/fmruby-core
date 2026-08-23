/**
 * @file fmrb_gfx_cmd.h
 * @brief Building and submitting the graphics commands the bindings send.
 *
 * Every language binding (mruby, Spinel, Python, Lua, BASIC) turns a call in
 * its own language into a gfx_cmd_t and hands it to the host task. Both halves
 * of that are the same for all of them, so they live here instead of being
 * copied per binding: the constructors below pack the command, and
 * fmrb_gfx_submit() queues it.
 *
 * A binding keeps only the part that is language specific - pulling the
 * arguments out of mrb_get_args / lua_tointeger / mp_obj_get_int / the BASIC
 * evaluator - and never touches a gfx_cmd_t field itself.
 *
 * Each constructor clears the whole command first, so a caller cannot leak a
 * stale sync pointer or unset field into the queue.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fmrb_err.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_rtos.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the host queue flow-control semaphore.
 *
 * The semaphore belongs to the host task, which creates it during its own
 * init and registers it here. Until then fmrb_gfx_submit() sends without back
 * pressure, so an early command cannot deadlock on a semaphore that does not
 * exist yet.
 *
 * @param sem Counting semaphore with one token per available GFX queue slot.
 */
void fmrb_gfx_set_flow_semaphore(fmrb_semaphore_t sem);

/**
 * @brief Hand one graphics command to the host task.
 *
 * Takes a flow-control token before queueing, so app drawing stays inside its
 * share of the host queue and HID events always have room. Blocking here is
 * the intended behaviour when an app draws faster than the graphics board can
 * consume; the host task returns the token once it has taken the command.
 *
 * @param cmd Command to send. Copied into the message payload.
 * @return FMRB_OK on success, FMRB_ERR_INVALID_STATE when called outside an
 *         app task, FMRB_ERR_TIMEOUT when the token or the queue times out.
 */
fmrb_err_t fmrb_gfx_submit(const gfx_cmd_t *cmd);

/**
 * @brief Queue a command without taking a flow-control slot.
 *
 * For the callers that are not the app whose canvas they touch: the kernel's
 * reap and the forced-kill path both release a dying app's canvases, and
 * neither may block on a semaphore whose refill is paced by app drawing. The
 * command carries a marker so the host task does not give a slot back for it,
 * which is what keeps take and give balanced.
 *
 * Everything an app itself issues must go through fmrb_gfx_submit() instead,
 * so that it stays inside the app's share of the host queue.
 *
 * @param cmd Command to send. Copied into the message payload.
 * @return FMRB_OK, or FMRB_ERR_TIMEOUT when the host queue stays full.
 */
fmrb_err_t fmrb_gfx_submit_unmetered(const gfx_cmd_t *cmd);

/* ---- command constructors ----------------------------------------------
 *
 * All of them take the command to fill and the target canvas, and the
 * remaining arguments in the order the matching FmrbGfx method takes them.
 * Parameters use the width of the field they land in, so a binding casts once
 * at the call site exactly as it does today.
 */

/** @brief Fill the whole canvas with one colour. */
void fmrb_gfx_cmd_clear(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                        fmrb_color_t color);

/** @brief Set a single pixel. */
void fmrb_gfx_cmd_pixel(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                        int16_t x, int16_t y, fmrb_color_t color);

/**
 * @brief Read a single pixel back (synchronous).
 * @param sync Sync context on the caller's stack; the caller blocks on it
 *             until the host task fills in the response.
 */
void fmrb_gfx_cmd_get_pixel(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                            int16_t x, int16_t y,
                            struct gfx_cmd_sync_ctx *sync);

/** @brief Draw a line between two points. */
void fmrb_gfx_cmd_line(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                       int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                       fmrb_color_t color);

/** @brief Draw a rectangle, outlined or filled. */
void fmrb_gfx_cmd_rect(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                       int16_t x, int16_t y, uint16_t w, uint16_t h,
                       fmrb_color_t color, bool filled);

/** @brief Blend a rectangle into the canvas with FMRB_BLEND_MODE_* mode. */
void fmrb_gfx_cmd_blend_rect(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                             int16_t x, int16_t y, uint16_t w, uint16_t h,
                             fmrb_color_t color, uint8_t mode);

/** @brief Draw a rounded rectangle, outlined or filled. */
void fmrb_gfx_cmd_round_rect(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                             int16_t x, int16_t y, int16_t w, int16_t h,
                             int16_t radius, fmrb_color_t color, bool filled);

/** @brief Draw a circle, outlined or filled. */
void fmrb_gfx_cmd_circle(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                         int16_t x, int16_t y, int16_t radius,
                         fmrb_color_t color, bool filled);

/** @brief Draw an ellipse, outlined or filled. */
void fmrb_gfx_cmd_ellipse(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                          int16_t x, int16_t y, int16_t rx, int16_t ry,
                          fmrb_color_t color, bool filled);

/** @brief Draw a triangle, outlined or filled. */
void fmrb_gfx_cmd_triangle(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                           int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                           int16_t x2, int16_t y2, fmrb_color_t color,
                           bool filled);

/** @brief Draw an arc between two radii and two angles. */
void fmrb_gfx_cmd_arc(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                      int16_t x, int16_t y, int16_t r0, int16_t r1,
                      int16_t angle0, int16_t angle1, fmrb_color_t color,
                      bool filled);

/**
 * @brief Draw a text run.
 *
 * The text is copied into the command's fixed buffer and truncated to
 * FMRB_GFX_MAX_TEXT_LEN - 1 characters, so no binding has to repeat the
 * bounds check. A NULL text draws nothing but still builds a valid command.
 *
 * @param bg_transparent true leaves the background untouched, false paints
 *                       bg_color behind the glyphs.
 * @param hybrid_mode 0 uses the current font, 1 renders ASCII/JA hybrid.
 */
void fmrb_gfx_cmd_text(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                       int16_t x, int16_t y, const char *text,
                       fmrb_color_t color, fmrb_color_t bg_color,
                       bool bg_transparent, fmrb_font_size_t font_size,
                       uint8_t hybrid_mode);

/**
 * @brief Draw a text run given as a pointer and a length.
 *
 * Same as fmrb_gfx_cmd_text() for a string that is not NUL terminated, which
 * is how the Spinel FFI hands over a Ruby string.
 */
void fmrb_gfx_cmd_text_n(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                         int16_t x, int16_t y, const char *text,
                         size_t text_len, fmrb_color_t color,
                         fmrb_color_t bg_color, bool bg_transparent,
                         fmrb_font_size_t font_size, uint8_t hybrid_mode);

/** @brief Set the text scale factor (1-4). */
void fmrb_gfx_cmd_text_size(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                            uint8_t size);

/** @brief Select the font family (0 = default, 1 = ja) and pixel height. */
void fmrb_gfx_cmd_set_font(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                           uint8_t family, uint8_t size);

/** @brief Composite the canvas onto the screen at (x, y). */
void fmrb_gfx_cmd_present(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                          int16_t x, int16_t y,
                          fmrb_color_t transparent_color);

/** @brief Blit an image, scaled by scale_*_fp8 (256 = 1.0, 0 y = same as x). */
void fmrb_gfx_cmd_draw_image(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                             uint16_t image_id, int16_t x, int16_t y,
                             uint8_t flags, int16_t scale_x_fp8,
                             int16_t scale_y_fp8);

/** @brief Blit an image through a mask. */
void fmrb_gfx_cmd_draw_image_masked(gfx_cmd_t *cmd,
                                    fmrb_canvas_handle_t canvas_id,
                                    uint16_t image_id, uint16_t mask_id,
                                    int16_t x, int16_t y);

/** @brief Stamp a sub-rect of a sprite image onto the canvas. */
void fmrb_gfx_cmd_draw_tile(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                            uint16_t image_id, int16_t src_x, int16_t src_y,
                            uint16_t w, uint16_t h, int16_t dst_x,
                            int16_t dst_y);

/** @brief Free an image. */
void fmrb_gfx_cmd_delete_image(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                               uint16_t image_id);

/** @brief Free a mask. */
void fmrb_gfx_cmd_delete_mask(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                              uint16_t mask_id);

/** @brief Set the NTSC luma output level. */
void fmrb_gfx_cmd_set_output_level(gfx_cmd_t *cmd,
                                   fmrb_canvas_handle_t canvas_id,
                                   uint8_t level);

/** @brief Set the NTSC chroma level. */
void fmrb_gfx_cmd_set_chroma_level(gfx_cmd_t *cmd,
                                   fmrb_canvas_handle_t canvas_id,
                                   uint8_t level);

/**
 * @brief Load a BMP file into a sprite image (synchronous on the host side).
 *
 * The path is copied into the command's fixed buffer and truncated to fit, so
 * no binding has to repeat the bounds check.
 */
void fmrb_gfx_cmd_load_sprite_image_bmp(gfx_cmd_t *cmd,
                                        fmrb_canvas_handle_t canvas_id,
                                        uint16_t image_id, const char *path);

/** @brief Same, for a path given as a pointer and a length. */
void fmrb_gfx_cmd_load_sprite_image_bmp_n(gfx_cmd_t *cmd,
                                          fmrb_canvas_handle_t canvas_id,
                                          uint16_t image_id, const char *path,
                                          size_t path_len);

/**
 * @brief Write the result of the last present to a file on the display side.
 *
 * The path names the display side's filesystem, which is the core's own on
 * Tab5 and a separate one in the simulator. Queued like a drawing command, so
 * placing it after a present saves that picture; the display side finishes the
 * write before it takes the next command. The path is copied into the
 * command's fixed buffer and truncated to fit.
 */
void fmrb_gfx_cmd_export_frame(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                               const char *path);

/** @brief Same, for a path given as a pointer and a length. */
void fmrb_gfx_cmd_export_frame_n(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                                 const char *path, size_t path_len);

/** @brief Free a sprite image. */
void fmrb_gfx_cmd_delete_sprite_image(gfx_cmd_t *cmd,
                                      fmrb_canvas_handle_t canvas_id,
                                      uint16_t image_id);

/** @brief Redirect drawing into a sprite image (0 = back to the canvas). */
void fmrb_gfx_cmd_set_sprite_image_target(gfx_cmd_t *cmd,
                                          fmrb_canvas_handle_t canvas_id,
                                          uint16_t image_id);

/** @brief Free a sprite instance. */
void fmrb_gfx_cmd_delete_sprite_instance(gfx_cmd_t *cmd,
                                         fmrb_canvas_handle_t canvas_id,
                                         uint16_t instance_id);

/** @brief Move a sprite instance. */
void fmrb_gfx_cmd_sprite_instance_move(gfx_cmd_t *cmd,
                                       fmrb_canvas_handle_t canvas_id,
                                       uint16_t instance_id, int16_t x,
                                       int16_t y);

/** @brief Show or hide a sprite instance. */
void fmrb_gfx_cmd_sprite_instance_set_visible(gfx_cmd_t *cmd,
                                              fmrb_canvas_handle_t canvas_id,
                                              uint16_t instance_id,
                                              bool visible);

/** @brief Select the animation frame of a sprite instance. */
void fmrb_gfx_cmd_sprite_instance_set_frame(gfx_cmd_t *cmd,
                                            fmrb_canvas_handle_t canvas_id,
                                            uint16_t instance_id,
                                            uint8_t frame_index);

/** @brief Free every sprite instance on the canvas. */
void fmrb_gfx_cmd_delete_all_sprites(gfx_cmd_t *cmd,
                                     fmrb_canvas_handle_t canvas_id);

#ifdef __cplusplus
}
#endif
