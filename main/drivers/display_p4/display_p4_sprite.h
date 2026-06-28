// Sprite manager for the ESP32-P4 / Tab5 display task.
// Mirrors the sprite_manager interface (fmruby-graphics-audio) but is a
// self-contained local implementation.
//
// SpriteImage: LGFX_Sprite (8bpp/RGB332, PSRAM) serving as a named bitmap
//   that can be drawn into and composited onto canvases.
// SpriteInstance: a positioned reference to one or more SpriteImages
//   (animation frames) that is rendered on top of its parent canvas.
// Mask: 1bpp bit-packed bitmap used for per-pixel masked blitting.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Pool limits
#define DISPLAY_P4_MAX_SPRITE_IMAGES    16
#define DISPLAY_P4_MAX_SPRITE_INSTANCES 32
#define DISPLAY_P4_MAX_MASKS             8

// Forward-declare to avoid pulling in heavy LovyanGFX headers.
namespace lgfx { class LGFX_Sprite; }

// ---------------------------------------------------------------------------
// SpriteImage
// ---------------------------------------------------------------------------

// Create a sprite image (8bpp PSRAM sprite).
// Returns image_id > 0 on success, 0 on failure.
uint16_t display_p4_sprite_image_create(uint16_t canvas_id,
                                         uint16_t width, uint16_t height,
                                         uint8_t transparent_color,
                                         bool use_transparent);

// Return the LGFX_Sprite for drawing into an image. NULL if not found.
lgfx::LGFX_Sprite* display_p4_sprite_image_get(uint16_t image_id);

// True if the image uses color-key transparency; out_color receives the key.
bool display_p4_sprite_image_get_transparent(uint16_t image_id,
                                              uint8_t *out_color);

// Delete a single image (also removes instances referencing it).
void display_p4_sprite_image_delete(uint16_t image_id);

// Delete all images and instances belonging to canvas_id.
void display_p4_sprite_delete_all_for_canvas(uint16_t canvas_id);

// ---------------------------------------------------------------------------
// SpriteInstance
// ---------------------------------------------------------------------------

// Create a sprite instance. Returns instance_id > 0 on success, 0 on failure.
uint16_t display_p4_sprite_instance_create(uint16_t canvas_id,
                                            const uint16_t *image_ids,
                                            uint8_t frame_count,
                                            int16_t x, int16_t y,
                                            int16_t z_order);

void display_p4_sprite_instance_delete(uint16_t instance_id);
void display_p4_sprite_instance_move(uint16_t instance_id, int16_t x, int16_t y);
void display_p4_sprite_instance_set_visible(uint16_t instance_id, bool visible);
void display_p4_sprite_instance_set_frame(uint16_t instance_id, uint8_t frame_index);

// Composite visible instances for canvas_id onto the destination LGFX_Sprite
// (the canvas sprite), sorted by z_order.
void display_p4_sprite_composite(uint16_t canvas_id,
                                  lgfx::LGFX_Sprite *canvas_sprite);

// ---------------------------------------------------------------------------
// Mask
// ---------------------------------------------------------------------------

// Allocate a 1bpp mask buffer. Returns mask_id > 0, or 0 on failure.
uint16_t display_p4_mask_create(uint16_t canvas_id,
                                 uint16_t width, uint16_t height);

// Append chunk to an existing mask buffer (offset in bytes).
void display_p4_mask_data(uint16_t mask_id,
                           const uint8_t *chunk, uint16_t chunk_len,
                           uint32_t offset);

void display_p4_mask_delete(uint16_t mask_id);

// Blit src_sprite → dst_sprite at (x, y) using the 1bpp mask.
void display_p4_mask_blit(uint16_t mask_id,
                           lgfx::LGFX_Sprite *src_sprite,
                           lgfx::LGFX_Sprite *dst_sprite,
                           int16_t x, int16_t y);
