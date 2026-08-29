// Sprite manager for the ESP32-P4 / Tab5 display task.
// Logic mirrors fmruby-graphics-audio/main/graphics/sprite_manager.cpp and
// graphics_handler.cpp but is a self-contained local implementation.

#include "display_p4_sprite.h"

#include <cstring>
#include "esp_heap_caps.h"
#ifndef FMRB_PLATFORM_WASM
#include "esp_private/esp_cache_private.h"
#endif
#include "fmrb_attr.h"
#include <cstdlib>   // qsort

#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_link_protocol.h"

static const char *TAG = "display_p4_spr";

// ============================================================
// SpriteImage pool
// ============================================================

typedef struct {
    bool         in_use;
    uint16_t     image_id;
    uint16_t     canvas_id;
    LGFX_Sprite *sprite;
    uint16_t     width, height;
    uint8_t      transparent_color;
    bool         use_transparent;
} p4_sprite_image_t;

// Pools are large (see display_p4_sprite.h); keep them in PSRAM
FMRB_EXT_RAM_BSS_ATTR static p4_sprite_image_t g_images[DISPLAY_P4_MAX_SPRITE_IMAGES];
static uint16_t g_next_image_id = 1;

static p4_sprite_image_t* image_find(uint16_t id) {
    for (int i = 0; i < DISPLAY_P4_MAX_SPRITE_IMAGES; i++) {
        if (g_images[i].in_use && g_images[i].image_id == id)
            return &g_images[i];
    }
    return nullptr;
}

uint16_t display_p4_sprite_image_create(uint16_t canvas_id,
                                         uint16_t width, uint16_t height,
                                         uint8_t transparent_color,
                                         bool use_transparent) {
    // Find free slot
    p4_sprite_image_t *slot = nullptr;
    for (int i = 0; i < DISPLAY_P4_MAX_SPRITE_IMAGES; i++) {
        if (!g_images[i].in_use) { slot = &g_images[i]; break; }
    }
    if (!slot) {
        FMRB_LOGE(TAG, "Sprite image pool full");
        return 0;
    }

    // Cache-aligned buffer for PPA compatibility
    size_t buf_size = (size_t)width * height * 2;
    size_t cache_line_size = 64;
#ifndef FMRB_PLATFORM_WASM
    esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &cache_line_size);
#endif
    buf_size = (buf_size + cache_line_size - 1) & ~(cache_line_size - 1);
    void *buf = heap_caps_aligned_alloc(cache_line_size, buf_size,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        FMRB_LOGE(TAG, "Sprite image alloc failed: %ux%u", (unsigned)width, (unsigned)height);
        return 0;
    }
    memset(buf, 0, buf_size);

    auto *spr = new LGFX_Sprite();
    // PPA-native RGB565 (non-byte-swapped). Depth must be set BEFORE
    // setBuffer: setColorDepth() on a sprite that already has a buffer
    // deletes it and reallocates internally (leak + wrong memory for PPA).
    spr->setColorDepth(lgfx::rgb565_nonswapped);
    spr->setBuffer(buf, width, height);

    uint16_t id = g_next_image_id++;
    if (id == 0) id = g_next_image_id++;

    slot->in_use           = true;
    slot->image_id         = id;
    slot->canvas_id        = canvas_id;
    slot->sprite           = spr;
    slot->width            = width;
    slot->height           = height;
    slot->transparent_color = transparent_color;
    slot->use_transparent   = use_transparent;

    FMRB_LOGI(TAG, "Image create: id=%u %ux%u canvas=%u", id, width, height, canvas_id);
    return id;
}

lgfx::LGFX_Sprite* display_p4_sprite_image_get(uint16_t image_id) {
    p4_sprite_image_t *img = image_find(image_id);
    return img ? img->sprite : nullptr;
}

bool display_p4_sprite_image_get_transparent(uint16_t image_id, uint8_t *out_color) {
    p4_sprite_image_t *img = image_find(image_id);
    if (!img) return false;
    if (img->use_transparent && out_color) *out_color = img->transparent_color;
    return img->use_transparent;
}

static void image_destroy(p4_sprite_image_t *img) {
    if (!img || !img->in_use) return;
    if (img->sprite) {
        void *buf = img->sprite->getBuffer();
        img->sprite->deleteSprite();
        delete img->sprite;
        img->sprite = nullptr;
        if (buf) heap_caps_free(buf);
    }
    img->in_use = false;
    FMRB_LOGI(TAG, "Image free: id=%u", img->image_id);
}

void display_p4_sprite_image_delete(uint16_t image_id) {
    // Also invalidate instances that reference this image
    // (handled in display_p4_sprite.cpp - instances will skip missing images)
    image_destroy(image_find(image_id));
}

// ============================================================
// SpriteInstance pool
// ============================================================

typedef struct {
    bool     in_use;
    uint16_t instance_id;
    uint16_t canvas_id;
    uint16_t image_ids[FMRB_SPRITE_MAX_FRAMES];
    uint8_t  frame_count;
    uint8_t  current_frame;
    int16_t  x, y;
    int16_t  z_order;
    bool     visible;
} p4_sprite_instance_t;

FMRB_EXT_RAM_BSS_ATTR static p4_sprite_instance_t g_instances[DISPLAY_P4_MAX_SPRITE_INSTANCES];
static uint16_t g_next_instance_id = 1;

// Scratch array for z-order sort in composite.
static p4_sprite_instance_t *g_sorted[DISPLAY_P4_MAX_SPRITE_INSTANCES];

static p4_sprite_instance_t* instance_find(uint16_t id) {
    for (int i = 0; i < DISPLAY_P4_MAX_SPRITE_INSTANCES; i++) {
        if (g_instances[i].in_use && g_instances[i].instance_id == id)
            return &g_instances[i];
    }
    return nullptr;
}

static int instance_z_cmp(const void *a, const void *b) {
    return (int)((*(const p4_sprite_instance_t **)a)->z_order)
         - (int)((*(const p4_sprite_instance_t **)b)->z_order);
}

uint16_t display_p4_sprite_instance_create(uint16_t canvas_id,
                                            const uint16_t *image_ids,
                                            uint8_t frame_count,
                                            int16_t x, int16_t y,
                                            int16_t z_order) {
    if (frame_count == 0 || frame_count > FMRB_SPRITE_MAX_FRAMES) {
        FMRB_LOGE(TAG, "Instance create: invalid frame_count=%u", frame_count);
        return 0;
    }

    p4_sprite_instance_t *slot = nullptr;
    for (int i = 0; i < DISPLAY_P4_MAX_SPRITE_INSTANCES; i++) {
        if (!g_instances[i].in_use) { slot = &g_instances[i]; break; }
    }
    if (!slot) {
        FMRB_LOGE(TAG, "Sprite instance pool full");
        return 0;
    }

    uint16_t id = g_next_instance_id++;
    if (id == 0) id = g_next_instance_id++;

    slot->in_use        = true;
    slot->instance_id   = id;
    slot->canvas_id     = canvas_id;
    slot->frame_count   = frame_count;
    slot->current_frame = 0;
    slot->x             = x;
    slot->y             = y;
    slot->z_order       = z_order;
    slot->visible       = true;
    memcpy(slot->image_ids, image_ids, sizeof(uint16_t) * frame_count);

    // One line per sprite instance, and a game makes them by the dozen while
    // it runs -- too much for a normal capture to stay readable. The failure
    // cases above stay at error level.
    FMRB_LOGD(TAG, "Instance create: id=%u canvas=%u frames=%u pos=(%d,%d) z=%d",
              id, canvas_id, frame_count, (int)x, (int)y, (int)z_order);
    return id;
}

void display_p4_sprite_instance_delete(uint16_t instance_id) {
    p4_sprite_instance_t *inst = instance_find(instance_id);
    if (!inst) return;
    inst->in_use = false;
    FMRB_LOGI(TAG, "Instance delete: id=%u", instance_id);
}

void display_p4_sprite_instance_move(uint16_t instance_id, int16_t x, int16_t y) {
    p4_sprite_instance_t *inst = instance_find(instance_id);
    if (inst) { inst->x = x; inst->y = y; }
}

void display_p4_sprite_instance_set_visible(uint16_t instance_id, bool visible) {
    p4_sprite_instance_t *inst = instance_find(instance_id);
    if (inst) inst->visible = visible;
}

void display_p4_sprite_instance_set_frame(uint16_t instance_id, uint8_t frame_index) {
    p4_sprite_instance_t *inst = instance_find(instance_id);
    if (inst && frame_index < inst->frame_count) inst->current_frame = frame_index;
}

void display_p4_sprite_delete_all_for_canvas(uint16_t canvas_id) {
    int ni = 0, nm = 0;
    for (int i = 0; i < DISPLAY_P4_MAX_SPRITE_INSTANCES; i++) {
        if (g_instances[i].in_use && g_instances[i].canvas_id == canvas_id) {
            g_instances[i].in_use = false;
            ni++;
        }
    }
    for (int i = 0; i < DISPLAY_P4_MAX_SPRITE_IMAGES; i++) {
        if (g_images[i].in_use && g_images[i].canvas_id == canvas_id) {
            image_destroy(&g_images[i]);
            nm++;
        }
    }
    if (ni || nm) {
        FMRB_LOGI(TAG, "Delete all for canvas %u: %d images, %d instances", canvas_id, nm, ni);
    }
}

void display_p4_sprite_composite(uint16_t canvas_id,
                                  lgfx::LGFX_Sprite *target,
                                  int16_t offset_x, int16_t offset_y) {
    if (!target) return;

    int count = 0;
    for (int i = 0; i < DISPLAY_P4_MAX_SPRITE_INSTANCES; i++) {
        p4_sprite_instance_t *inst = &g_instances[i];
        if (inst->in_use && inst->visible && inst->canvas_id == canvas_id) {
            g_sorted[count++] = inst;
        }
    }
    if (count == 0) return;

    if (count > 1) {
        qsort(g_sorted, (size_t)count, sizeof(p4_sprite_instance_t *), instance_z_cmp);
    }

    for (int i = 0; i < count; i++) {
        p4_sprite_instance_t *inst = g_sorted[i];
        p4_sprite_image_t *img = image_find(inst->image_ids[inst->current_frame]);
        if (!img || !img->sprite) continue;

        int16_t dx = inst->x + offset_x;
        int16_t dy = inst->y + offset_y;
        if (img->use_transparent) {
            img->sprite->pushSprite(target, dx, dy,
                                    (uint32_t)img->transparent_color);
        } else {
            img->sprite->pushSprite(target, dx, dy);
        }
    }
}

// ============================================================
// Mask pool
// ============================================================

typedef struct {
    bool     in_use;
    uint16_t mask_id;
    uint16_t canvas_id;
    uint16_t width, height;
    uint32_t data_len;
    uint8_t *data;   // bit-packed, MSB-first; fmrb_sys_malloc
} p4_mask_t;

static p4_mask_t g_masks[DISPLAY_P4_MAX_MASKS];
static uint16_t g_next_mask_id = 1;

static p4_mask_t* mask_find(uint16_t id) {
    for (int i = 0; i < DISPLAY_P4_MAX_MASKS; i++) {
        if (g_masks[i].in_use && g_masks[i].mask_id == id)
            return &g_masks[i];
    }
    return nullptr;
}

static void mask_destroy(p4_mask_t *m) {
    if (!m || !m->in_use) return;
    if (m->data) {
        fmrb_sys_free(m->data);
        m->data = nullptr;
    }
    m->in_use = false;
    FMRB_LOGI(TAG, "Mask free: id=%u", m->mask_id);
}

uint16_t display_p4_mask_create(uint16_t canvas_id,
                                 uint16_t width, uint16_t height) {
    p4_mask_t *slot = nullptr;
    for (int i = 0; i < DISPLAY_P4_MAX_MASKS; i++) {
        if (!g_masks[i].in_use) { slot = &g_masks[i]; break; }
    }
    if (!slot) {
        FMRB_LOGE(TAG, "Mask pool full");
        return 0;
    }

    uint32_t row_bytes = ((uint32_t)width + 7) / 8;
    uint32_t data_len  = row_bytes * height;
    uint8_t *buf = (uint8_t *)fmrb_sys_malloc(data_len);
    if (!buf) {
        FMRB_LOGE(TAG, "Mask alloc failed: %ux%u (%lu bytes)",
                  (unsigned)width, (unsigned)height, (unsigned long)data_len);
        return 0;
    }
    memset(buf, 0, data_len);

    uint16_t id = g_next_mask_id++;
    if (id == 0) id = g_next_mask_id++;

    slot->in_use   = true;
    slot->mask_id  = id;
    slot->canvas_id = canvas_id;
    slot->width    = width;
    slot->height   = height;
    slot->data_len = data_len;
    slot->data     = buf;

    FMRB_LOGI(TAG, "Mask create: id=%u %ux%u canvas=%u", id, width, height, canvas_id);
    return id;
}

void display_p4_mask_data(uint16_t mask_id,
                           const uint8_t *chunk, uint16_t chunk_len,
                           uint32_t offset) {
    p4_mask_t *m = mask_find(mask_id);
    if (!m) return;
    if (offset + chunk_len > m->data_len) {
        FMRB_LOGE(TAG, "Mask data overflow: id=%u off=%lu+%u > %lu",
                  mask_id, (unsigned long)offset, (unsigned)chunk_len,
                  (unsigned long)m->data_len);
        return;
    }
    memcpy(m->data + offset, chunk, chunk_len);
}

void display_p4_mask_delete(uint16_t mask_id) {
    mask_destroy(mask_find(mask_id));
}

void display_p4_mask_blit(uint16_t mask_id,
                           lgfx::LGFX_Sprite *src_sprite,
                           lgfx::LGFX_Sprite *dst_sprite,
                           int16_t x, int16_t y) {
    p4_mask_t *m = mask_find(mask_id);
    if (!m || !src_sprite || !dst_sprite) return;

    int mw = m->width;
    int mh = m->height;
    int sw = src_sprite->width();
    int sh = src_sprite->height();
    int row_bytes = (mw + 7) / 8;

    for (int yy = 0; yy < mh && yy < sh; yy++) {
        const uint8_t *row = m->data + yy * row_bytes;
        for (int xx = 0; xx < mw && xx < sw; xx++) {
            uint8_t bit = (row[xx >> 3] >> (7 - (xx & 7))) & 1;
            if (!bit) continue;
            // readPixelValue returns RGB565 value for 16bpp sprites
            uint16_t pixel = (uint16_t)src_sprite->readPixelValue(xx, yy);
            dst_sprite->drawPixel(x + xx, y + yy, pixel);
        }
    }
}
