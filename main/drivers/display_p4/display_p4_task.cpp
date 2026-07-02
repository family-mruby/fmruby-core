// Family mruby Modern (ESP32-P4 / M5Stack Tab5) display task.
//
// Receives msgpack-encoded GFX/CONTROL commands from the hal_link_local TX
// buffer, dispatches them, and sends ACK responses via the RX buffer.
//
// All internal buffers use RGB565 (16bpp) for PPA hardware acceleration.
// GFX commands carry RGB332 color values which are converted to RGB565
// at the command dispatch layer.

#include "display_p4_task.h"
#include "display_p4_vm.h"
#include "display_p4_sprite.h"
#include "lgfx_tab5.hpp"

#include "fmrb_log.h"
#include "fmrb_hal_link.h"
#include "fmrb_link_protocol.h"
#include "fmrb_link_types.h"
#include "fmrb_mem.h"
#include "fmrb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"

extern "C" {
#include "fmrb_bmp332.h"
}

#include <msgpack.h>
#include <cstring>
#include <cstdlib>  // qsort
#include "esp_private/esp_cache_private.h"
#include "esp_cache.h"

static const char *TAG = "display_p4";

// PPA-native RGB565 (non-byte-swapped), matching PPA hardware I/O format.
// LovyanGFX setBuffer(buf, w, h, 16) defaults to rgb565_2Byte (byte-swapped),
// which causes double-swap on PPA Blend output. Override to non-swapped so that
// all buffers use the same pixel format as PPA hardware.
static void set_ppa_native_depth(LGFX_Sprite *sprite) {
    sprite->setColorDepth(lgfx::rgb565_nonswapped);
}

// Allocate cache-aligned buffer for PPA DMA compatibility.
// Uses esp_cache_get_alignment() to query the actual cache line size
// (L2 cache on Tab5 is 128B, not 64B).
static void* ppa_alloc_buffer(size_t length, size_t *out_aligned_size) {
    size_t cache_line_size = 64;
    esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &cache_line_size);
    size_t aligned = (length + cache_line_size - 1) & ~(cache_line_size - 1);
    void *buf = heap_caps_aligned_alloc(cache_line_size, aligned,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf) memset(buf, 0, aligned);
    if (out_aligned_size) *out_aligned_size = buf ? aligned : 0;
    return buf;
}

// Tab5 board pins / I2C (see fmrb_pin_assign.h; kept local to the driver).
#define TAB5_I2C_PORT   I2C_NUM_1
#define TAB5_I2C_SDA    GPIO_NUM_31
#define TAB5_I2C_SCL    GPIO_NUM_32
#define TAB5_TP_INT     GPIO_NUM_23
#define PI4IO1_ADDR     0x43   // PI4IO GPIO expander #1 (LCD/touch reset)
#define PI4IO2_ADDR     0x44   // PI4IO GPIO expander #2 (power rails)

static LGFX_Tab5 g_lcd;
static volatile bool g_lcd_ready = false;

// Receive buffer for local link commands
#define DISPLAY_P4_RECV_BUF_SIZE 4096
static uint8_t g_recv_buf[DISPLAY_P4_RECV_BUF_SIZE];

// ============================================================
// Canvas management
// Each canvas is an 8bpp (RGB332) LGFX_Sprite allocated in PSRAM.
// ============================================================

#define DISPLAY_P4_MAX_CANVAS    8
#define DISPLAY_P4_CANVAS_SCREEN  ((uint16_t)0x0000)
#define DISPLAY_P4_CANVAS_RENDER  ((uint16_t)0xFFF0)
#define DISPLAY_P4_CANVAS_INVALID ((uint16_t)0xFFFF)

typedef struct {
    uint16_t     canvas_id;
    LGFX_Sprite *sprite;
    int16_t      z_order;
    int16_t      push_x, push_y;
    bool         is_visible;
    // Creation-time transparency (used during render_frame compositing)
    uint8_t      create_transparent_color;     // Original RGB332 value
    // PPA Blend color-key range (RGB888). PPA internally expands RGB565 to
    // RGB888 before comparison; range accounts for quantization ambiguity.
    uint8_t      create_ck_r_low, create_ck_r_high;
    uint8_t      create_ck_g_low, create_ck_g_high;
    uint8_t      create_ck_b_low, create_ck_b_high;
    bool         create_use_transparency;
    // Push-time transparency (used for canvas-to-canvas PUSH_CANVAS only)
    uint8_t      transparent_color;
    bool         use_transparency;
    uint16_t     width, height;
    // Cache-line-aligned size of the sprite buffer (esp_cache_msync requires
    // both address and size aligned to the cache line, or it fails silently)
    size_t       buf_aligned_size;
} p4_canvas_t;

static p4_canvas_t g_canvases[DISPLAY_P4_MAX_CANVAS];
static size_t      g_canvas_count = 0;
static uint16_t    g_next_canvas_id = 1;

// When non-zero, draw commands are redirected to this SpriteImage's sprite.
static uint16_t    g_sprite_target_id = 0;

// ============================================================
// PNG image store (CREATE_IMAGE_FROM_FILE / DRAW_IMAGE / DELETE_IMAGE)
// Separate ID space from SpriteImage. PNG files are decoded into
// LGFX_Sprite objects (8bpp RGB332 in PSRAM).
// ============================================================

#define DISPLAY_P4_MAX_IMAGES 8

typedef struct {
    bool         in_use;
    uint16_t     image_id;
    LGFX_Sprite *sprite;
    uint16_t     width, height;
} p4_image_t;

static p4_image_t g_images_store[DISPLAY_P4_MAX_IMAGES];
static uint16_t   g_next_image_store_id = 1;

static p4_image_t* image_store_find(uint16_t id) {
    for (int i = 0; i < DISPLAY_P4_MAX_IMAGES; i++) {
        if (g_images_store[i].in_use && g_images_store[i].image_id == id)
            return &g_images_store[i];
    }
    return nullptr;
}

static void image_store_destroy(p4_image_t *img) {
    if (!img || !img->in_use) return;
    if (img->sprite) {
        img->sprite->deleteSprite();
        delete img->sprite;
        img->sprite = nullptr;
    }
    img->in_use = false;
    FMRB_LOGI(TAG, "Image store free: id=%u", img->image_id);
}

// Shared composition framebuffer (RGB565 16bpp, allocated at INIT_DISPLAY).
// Canvases are composited via pushSprite, then PPA SRM 3x scales to LCD.
static LGFX_Sprite *g_framebuffer = nullptr;
static size_t g_fb_aligned_size = 0;  // cache-line-aligned framebuffer size for msync
static uint16_t g_display_width  = 0;
static uint16_t g_display_height = 0;
#define DISPLAY_P4_SCALE_FACTOR 3

// PPA (Pixel Processing Accelerator) for hardware operations
static ppa_client_handle_t g_ppa_srm = NULL;    // Scale-Rotate-Mirror (3x scaling)
static ppa_client_handle_t g_ppa_blend = NULL;  // Blend (canvas compositing with color-key)
static void *g_ppa_out_buf = NULL;
static size_t g_ppa_out_buf_size = 0;

static p4_canvas_t* canvas_find(uint16_t canvas_id) {
    for (size_t i = 0; i < g_canvas_count; i++) {
        if (g_canvases[i].canvas_id == canvas_id)
            return &g_canvases[i];
    }
    return nullptr;
}

static p4_canvas_t* canvas_alloc(uint16_t canvas_id,
                                  uint16_t width, uint16_t height, int16_t z_order,
                                  uint8_t transparent_color, bool use_transparency) {
    if (g_canvas_count >= DISPLAY_P4_MAX_CANVAS) {
        FMRB_LOGE(TAG, "Canvas pool full (max %d)", DISPLAY_P4_MAX_CANVAS);
        return nullptr;
    }

    size_t buf_size = (size_t)width * height * 2;  // RGB565 = 2 bytes/pixel
    size_t aligned_size = 0;
    void *buf = ppa_alloc_buffer(buf_size, &aligned_size);
    if (!buf) {
        FMRB_LOGE(TAG, "Canvas buffer alloc failed: %dx%d (%u bytes)",
                  (int)width, (int)height, (unsigned)buf_size);
        return nullptr;
    }

    auto *sprite = new LGFX_Sprite(&g_lcd);
    // Depth must be set BEFORE setBuffer: LGFX_Sprite::setColorDepth() on a
    // sprite that already has a buffer deletes it and reallocates internally
    // (leaking the aligned PPA buffer and moving pixels to internal RAM).
    set_ppa_native_depth(sprite);
    sprite->setBuffer(buf, width, height);

    p4_canvas_t *c              = &g_canvases[g_canvas_count++];
    c->canvas_id                = canvas_id;
    c->sprite                   = sprite;
    c->z_order                  = z_order;
    c->push_x                   = 0;
    c->push_y                   = 0;
    c->is_visible               = false;
    c->create_transparent_color     = transparent_color;
    // Convert RGB332 -> RGB565(nonswapped) -> extract R5/G6/B5 -> RGB888 range
    // for PPA Blend color-key. PPA expands RGB565 to RGB888 internally;
    // range covers the quantization ambiguity in the lower bits.
    lgfx::rgb332_t tc; tc.raw = transparent_color;
    lgfx::rgb565_t tc565; tc565 = tc;
    c->create_ck_r_low  = tc565.r5 << 3;
    c->create_ck_r_high = (tc565.r5 << 3) | 0x07;
    c->create_ck_g_low  = tc565.g6 << 2;
    c->create_ck_g_high = (tc565.g6 << 2) | 0x03;
    c->create_ck_b_low  = tc565.b5 << 3;
    c->create_ck_b_high = (tc565.b5 << 3) | 0x07;
    c->create_use_transparency      = use_transparency;
    c->transparent_color            = transparent_color;
    c->use_transparency             = use_transparency;
    c->width                    = width;
    c->height                   = height;
    c->buf_aligned_size         = aligned_size;

    FMRB_LOGI(TAG, "Canvas alloc: id=%u %dx%d z=%d transp=%u/%u",
              canvas_id, width, height, z_order, transparent_color, (uint8_t)use_transparency);
    return c;
}

static void canvas_free(p4_canvas_t *c) {
    if (!c) return;
    FMRB_LOGI(TAG, "Canvas free: id=%u", c->canvas_id);

    display_p4_vm_delete_progs_by_canvas(c->canvas_id);
    display_p4_sprite_delete_all_for_canvas(c->canvas_id);

    if (c->sprite) {
        void *buf = c->sprite->getBuffer();
        c->sprite->deleteSprite();
        delete c->sprite;
        c->sprite = nullptr;
        if (buf) heap_caps_free(buf);
    }

    size_t idx = (size_t)(c - g_canvases);
    if (idx < g_canvas_count - 1) {
        memmove(&g_canvases[idx], &g_canvases[idx + 1],
                (g_canvas_count - idx - 1) * sizeof(p4_canvas_t));
    }
    g_canvas_count--;
}

static int canvas_cmp_zorder(const void *a, const void *b) {
    return (int)((const p4_canvas_t *)a)->z_order
         - (int)((const p4_canvas_t *)b)->z_order;
}

// Return the drawing target sprite. When g_sprite_target_id is set, drawing
// commands are redirected to that SpriteImage rather than the canvas sprite.
static LGFX_Sprite* get_sprite(uint16_t canvas_id) {
    if (g_sprite_target_id != 0) {
        LGFX_Sprite *s = display_p4_sprite_image_get(g_sprite_target_id);
        if (s) return s;
        // Target image gone; fall through to canvas
    }
    p4_canvas_t *c = canvas_find(canvas_id);
    return c ? c->sprite : nullptr;
}

// ============================================================
// Mouse cursor (16x16 sprite, drawn last on each render)
// ============================================================

static LGFX_Sprite *g_cursor_sprite  = nullptr;
static bool         g_cursor_visible = false;
static int          g_cursor_x = 0;
static int          g_cursor_y = 0;

#define CURSOR_TRANSPARENT 0xFF00FF

static void cursor_init(void) {
    g_cursor_sprite = new LGFX_Sprite(&g_lcd);
    set_ppa_native_depth(g_cursor_sprite);
    g_cursor_sprite->setPsram(false);
    g_cursor_sprite->createSprite(16, 16);
    g_cursor_sprite->clear((uint32_t)CURSOR_TRANSPARENT);

    static const uint8_t pat[16][16] = {
        {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0},
        {1,2,2,2,2,2,2,2,2,2,1,0,0,0,0,0},
        {1,2,2,2,2,2,1,1,1,1,1,1,0,0,0,0},
        {1,2,2,1,2,2,1,0,0,0,0,0,0,0,0,0},
        {1,2,1,0,1,2,2,1,0,0,0,0,0,0,0,0},
        {1,1,0,0,1,2,2,1,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,1,2,2,1,0,0,0,0,0,0,0},
        {0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0},
    };
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            uint32_t color;
            switch (pat[y][x]) {
                case 1:  color = 0xFFFFFF; break;
                case 2:  color = 0x000000; break;
                default: color = CURSOR_TRANSPARENT; break;
            }
            g_cursor_sprite->drawPixel(x, y, color);
        }
    }
}

// ============================================================
// Render frame: composite all visible canvases + sprite instances
// into the shared framebuffer, then push to g_lcd with 3x scaling.
//
// Canvas sprites are NOT modified — sprite instances are composited
// directly into the framebuffer after the canvas, keeping the canvas
// buffer clean for subsequent frames.
// ============================================================

static bool g_first_render = true;

static void render_frame(void) {
    if (!g_framebuffer || g_canvas_count == 0) return;

    if (g_first_render) {
        g_lcd.fillScreen(0);
        g_first_render = false;
    }

    g_framebuffer->clear(0);

    if (g_canvas_count > 1) {
        qsort(g_canvases, g_canvas_count, sizeof(p4_canvas_t), canvas_cmp_zorder);
    }

    int fb_w = g_framebuffer->width();
    int fb_h = g_framebuffer->height();

    for (size_t i = 0; i < g_canvas_count; i++) {
        p4_canvas_t *c = &g_canvases[i];
        if (!c->is_visible || !c->sprite) continue;

        int sw = c->sprite->width();
        int sh = c->sprite->height();

        // Clip canvas to framebuffer bounds
        int sx0 = 0, sy0 = 0, dx = c->push_x, dy = c->push_y;
        int cw = sw, ch = sh;
        if (dx < 0) { sx0 = -dx; cw += dx; dx = 0; }
        if (dy < 0) { sy0 = -dy; ch += dy; dy = 0; }
        if (dx + cw > fb_w) cw = fb_w - dx;
        if (dy + ch > fb_h) ch = fb_h - dy;

        if (cw > 0 && ch > 0 && g_ppa_blend) {
            void *fg_buf = c->sprite->getBuffer();
            void *bg_buf = g_framebuffer->getBuffer();
            // msync requires cache-line-aligned sizes; use the aligned
            // allocation sizes, not the raw pixel byte counts
            size_t fg_size = c->buf_aligned_size;
            size_t bg_size = g_fb_aligned_size;

            // Flush CPU cache to PSRAM so PPA DMA reads current pixel data
            esp_err_t sync_err;
            sync_err = esp_cache_msync(fg_buf, fg_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            if (sync_err != ESP_OK) FMRB_LOGE(TAG, "fg msync C2M failed: %d", sync_err);
            sync_err = esp_cache_msync(bg_buf, bg_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            if (sync_err != ESP_OK) FMRB_LOGE(TAG, "bg msync C2M failed: %d", sync_err);

            ppa_blend_oper_config_t blend = {};
            // Background: framebuffer
            blend.in_bg.buffer         = bg_buf;
            blend.in_bg.pic_w          = (uint32_t)fb_w;
            blend.in_bg.pic_h          = (uint32_t)fb_h;
            blend.in_bg.block_w        = (uint32_t)cw;
            blend.in_bg.block_h        = (uint32_t)ch;
            blend.in_bg.block_offset_x = (uint32_t)dx;
            blend.in_bg.block_offset_y = (uint32_t)dy;
            blend.in_bg.blend_cm       = PPA_BLEND_COLOR_MODE_RGB565;

            // Foreground: canvas
            blend.in_fg.buffer         = fg_buf;
            blend.in_fg.pic_w          = (uint32_t)sw;
            blend.in_fg.pic_h          = (uint32_t)sh;
            blend.in_fg.block_w        = (uint32_t)cw;
            blend.in_fg.block_h        = (uint32_t)ch;
            blend.in_fg.block_offset_x = (uint32_t)sx0;
            blend.in_fg.block_offset_y = (uint32_t)sy0;
            blend.in_fg.blend_cm       = PPA_BLEND_COLOR_MODE_RGB565;

            // Output: framebuffer (in-place, Blend allows BG==OUT)
            blend.out.buffer         = bg_buf;
            blend.out.buffer_size    = bg_size;
            blend.out.pic_w          = (uint32_t)fb_w;
            blend.out.pic_h          = (uint32_t)fb_h;
            blend.out.block_offset_x = (uint32_t)dx;
            blend.out.block_offset_y = (uint32_t)dy;
            blend.out.blend_cm       = PPA_BLEND_COLOR_MODE_RGB565;

            // All sprites use PPA-native RGB565 (non-swapped); no byte swap needed
            blend.fg_byte_swap = false;
            blend.bg_byte_swap = false;

            // FG fully opaque
            blend.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
            blend.fg_alpha_fix_val     = 255;
            blend.bg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;

            // Color-key for transparent canvas (range covers RGB565->RGB888 quantization)
            if (c->create_use_transparency) {
                blend.fg_ck_en = true;
                blend.fg_ck_rgb_low_thres  = {.b = c->create_ck_b_low,
                                               .g = c->create_ck_g_low,
                                               .r = c->create_ck_r_low};
                blend.fg_ck_rgb_high_thres = {.b = c->create_ck_b_high,
                                               .g = c->create_ck_g_high,
                                               .r = c->create_ck_r_high};
            }

            blend.mode = PPA_TRANS_MODE_BLOCKING;

            esp_err_t err = ppa_do_blend(g_ppa_blend, &blend);
            if (err != ESP_OK) {
                FMRB_LOGE(TAG, "PPA Blend failed: %d", err);
            }
            // Invalidate output cache so CPU sees DMA-written data
            esp_cache_msync(bg_buf, bg_size,
                            ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
        }

        display_p4_sprite_composite(c->canvas_id, g_framebuffer,
                                    c->push_x, c->push_y);
    }

    if (g_cursor_visible && g_cursor_sprite) {
        g_cursor_sprite->pushSprite(g_framebuffer, g_cursor_x, g_cursor_y,
                                    (uint32_t)CURSOR_TRANSPARENT);
    }

    // Scale framebuffer 3x to LCD via PPA SRM hardware accelerator.
    if (g_ppa_srm && g_ppa_out_buf) {
        int fb_w = g_framebuffer->width();
        int fb_h = g_framebuffer->height();
        void *fb_ptr = g_framebuffer->getBuffer();

        // Flush framebuffer cache before SRM DMA reads it
        esp_err_t sync_err = esp_cache_msync(fb_ptr, g_fb_aligned_size,
                                             ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        if (sync_err != ESP_OK) FMRB_LOGE(TAG, "fb msync C2M failed: %d", sync_err);

        int out_w = fb_w * DISPLAY_P4_SCALE_FACTOR;
        int out_h = fb_h * DISPLAY_P4_SCALE_FACTOR;

        ppa_srm_oper_config_t srm = {};
        srm.in.buffer         = fb_ptr;
        srm.in.pic_w          = (uint32_t)fb_w;
        srm.in.pic_h          = (uint32_t)fb_h;
        srm.in.block_w        = (uint32_t)fb_w;
        srm.in.block_h        = (uint32_t)fb_h;
        srm.in.block_offset_x = 0;
        srm.in.block_offset_y = 0;
        srm.in.srm_cm         = PPA_SRM_COLOR_MODE_RGB565;

        srm.out.buffer         = g_ppa_out_buf;
        srm.out.buffer_size    = g_ppa_out_buf_size;
        srm.out.pic_w          = (uint32_t)out_w;
        srm.out.pic_h          = (uint32_t)out_h;
        srm.out.block_offset_x = 0;
        srm.out.block_offset_y = 0;
        srm.out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565;

        srm.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
        srm.scale_x        = (float)DISPLAY_P4_SCALE_FACTOR;
        srm.scale_y        = (float)DISPLAY_P4_SCALE_FACTOR;
        srm.mirror_x       = false;
        srm.mirror_y       = false;
        srm.rgb_swap       = false;
        srm.byte_swap      = false;  // All buffers use PPA-native RGB565
        srm.mode           = PPA_TRANS_MODE_BLOCKING;

        esp_err_t err = ppa_do_scale_rotate_mirror(g_ppa_srm, &srm);
        if (err == ESP_OK) {
            // Invalidate SRM output cache so CPU reads DMA-written data
            esp_cache_msync(g_ppa_out_buf, g_ppa_out_buf_size,
                            ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
            int offset_x = (g_lcd.width() - out_w) / 2;
            g_lcd.pushImage(offset_x, 0, out_w, out_h, (lgfx::rgb565_t *)g_ppa_out_buf);
        } else {
            FMRB_LOGE(TAG, "PPA SRM failed: %d", err);
        }
    } else {
        // Fallback: software scaling
        int fb_w = g_framebuffer->width();
        int fb_h = g_framebuffer->height();
        int scaled_w = fb_w * DISPLAY_P4_SCALE_FACTOR;
        int scaled_h = fb_h * DISPLAY_P4_SCALE_FACTOR;
        int center_x = (g_lcd.width()  - scaled_w) / 2 + scaled_w / 2;
        int center_y = (g_lcd.height() - scaled_h) / 2 + scaled_h / 2;
        g_framebuffer->pushRotateZoom(&g_lcd, (float)center_x, (float)center_y,
                                      0.0f,
                                      (float)DISPLAY_P4_SCALE_FACTOR,
                                      (float)DISPLAY_P4_SCALE_FACTOR);
    }
}

// ============================================================
// PI4IO I2C helper
// ============================================================

static inline void pi4io_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    i2c_master_transmit(dev, buf, sizeof(buf), 100);
}

// ============================================================
// Power/reset sequence for the Tab5 panel + touch.
// ============================================================

static void tab5_power_on(void) {
    // TP INT high during reset selects GT911 touch I2C address 0x14.
    gpio_config_t int_cfg = {};
    int_cfg.pin_bit_mask = 1ULL << TAB5_TP_INT;
    int_cfg.mode = GPIO_MODE_OUTPUT;
    gpio_config(&int_cfg);
    gpio_set_level(TAB5_TP_INT, 1);

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = TAB5_I2C_PORT;
    bus_cfg.sda_io_num = TAB5_I2C_SDA;
    bus_cfg.scl_io_num = TAB5_I2C_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus = NULL;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        FMRB_LOGE(TAG, "PI4IO i2c bus init failed");
        return;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.scl_speed_hz = 100000;
    i2c_master_dev_handle_t io1 = NULL, io2 = NULL;
    dev_cfg.device_address = PI4IO1_ADDR;
    i2c_master_bus_add_device(bus, &dev_cfg, &io1);
    dev_cfg.device_address = PI4IO2_ADDR;
    i2c_master_bus_add_device(bus, &dev_cfg, &io2);

    // PI4IO #1: directions/pulls, resets driven LOW
    pi4io_write(io1, 0x03, 0x7F);
    pi4io_write(io1, 0x05, 0x46);
    pi4io_write(io1, 0x07, 0x00);
    pi4io_write(io1, 0x0D, 0x7F);
    pi4io_write(io1, 0x0B, 0x7F);
    // PI4IO #2: power rails / IO config
    pi4io_write(io2, 0x03, 0xB9);
    pi4io_write(io2, 0x07, 0x06);
    pi4io_write(io2, 0x0D, 0xB9);
    pi4io_write(io2, 0x0B, 0xF9);
    pi4io_write(io2, 0x09, 0x40);
    pi4io_write(io2, 0x11, 0xBF);
    pi4io_write(io2, 0x05, 0x89);

    vTaskDelay(pdMS_TO_TICKS(10));
    pi4io_write(io1, 0x05, 0x76);  // release LCD + touch resets

    gpio_set_direction(TAB5_TP_INT, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(100));

    i2c_master_bus_rm_device(io1);
    i2c_master_bus_rm_device(io2);
    i2c_del_master_bus(bus);
    // Backlight: LEDC PWM via Light_PWM in LGFX_Tab5 (GPIO22, ch7, 44100Hz).
}

// ============================================================
// ACK response sender
// ============================================================

static void send_ack(uint8_t msg_type, uint8_t seq, const uint8_t *data, size_t data_len) {
    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer pk;
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    msgpack_pack_array(&pk, 4);
    msgpack_pack_uint8(&pk, msg_type);
    msgpack_pack_uint8(&pk, seq);
    msgpack_pack_uint8(&pk, FMRB_LINK_RESPONSE_MSG_ACK);

    if (data && data_len > 0) {
        msgpack_pack_bin(&pk, data_len);
        msgpack_pack_bin_body(&pk, data, data_len);
    } else {
        msgpack_pack_nil(&pk);
    }

    fmrb_link_message_t resp = {
        .data = (uint8_t *)sbuf.data,
        .size = sbuf.size,
    };
    fmrb_hal_link_local_send_response(FMRB_LINK_CHANNEL_DEFAULT, &resp, 1000);
    msgpack_sbuffer_destroy(&sbuf);
}

// ============================================================
// GFX command dispatcher
// Returns: 0 = success, no ACK sent
//          1 = ACK with data already sent
//         -1 = error / unimplemented
// ============================================================

static int process_gfx_command(uint8_t msg_type, uint8_t sub_cmd, uint8_t seq,
                                const uint8_t *data, size_t size) {
    switch (sub_cmd) {

    // --- Screen-level fill ---

    case FMRB_LINK_GFX_CLEAR:
    case FMRB_LINK_GFX_FILL_SCREEN: {
        if (size < sizeof(fmrb_link_graphics_clear_t)) break;
        const auto *cmd = (const fmrb_link_graphics_clear_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->fillScreen(cmd->color);
        return 0;
    }

    // --- Pixel ---

    case FMRB_LINK_GFX_DRAW_PIXEL: {
        if (size < sizeof(fmrb_link_graphics_pixel_t)) break;
        const auto *cmd = (const fmrb_link_graphics_pixel_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawPixel(cmd->x, cmd->y, cmd->color);
        return 0;
    }

    // --- Lines ---

    case FMRB_LINK_GFX_DRAW_LINE: {
        if (size < sizeof(fmrb_link_graphics_line_t)) break;
        const auto *cmd = (const fmrb_link_graphics_line_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawLine(cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_FAST_VLINE: {
        if (size < sizeof(fmrb_link_graphics_line_t)) break;
        const auto *cmd = (const fmrb_link_graphics_line_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawFastVLine(cmd->x1, cmd->y1, cmd->y2 - cmd->y1, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_FAST_HLINE: {
        if (size < sizeof(fmrb_link_graphics_line_t)) break;
        const auto *cmd = (const fmrb_link_graphics_line_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawFastHLine(cmd->x1, cmd->y1, cmd->x2 - cmd->x1, cmd->color);
        return 0;
    }

    // --- Rectangles ---

    case FMRB_LINK_GFX_DRAW_RECT: {
        if (size < sizeof(fmrb_link_graphics_rect_t)) break;
        const auto *cmd = (const fmrb_link_graphics_rect_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_RECT: {
        if (size < sizeof(fmrb_link_graphics_rect_t)) break;
        const auto *cmd = (const fmrb_link_graphics_rect_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->fillRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_ROUND_RECT: {
        if (size < sizeof(fmrb_link_graphics_round_rect_t)) break;
        const auto *cmd = (const fmrb_link_graphics_round_rect_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawRoundRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->radius, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_ROUND_RECT: {
        if (size < sizeof(fmrb_link_graphics_round_rect_t)) break;
        const auto *cmd = (const fmrb_link_graphics_round_rect_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->fillRoundRect(cmd->x, cmd->y, cmd->width, cmd->height, cmd->radius, cmd->color);
        return 0;
    }

    // --- Circles / Ellipses ---

    case FMRB_LINK_GFX_DRAW_CIRCLE: {
        if (size < sizeof(fmrb_link_graphics_circle_t)) break;
        const auto *cmd = (const fmrb_link_graphics_circle_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawCircle(cmd->x, cmd->y, cmd->radius, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_CIRCLE: {
        if (size < sizeof(fmrb_link_graphics_circle_t)) break;
        const auto *cmd = (const fmrb_link_graphics_circle_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->fillCircle(cmd->x, cmd->y, cmd->radius, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_ELLIPSE: {
        if (size < sizeof(fmrb_link_graphics_ellipse_t)) break;
        const auto *cmd = (const fmrb_link_graphics_ellipse_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawEllipse(cmd->x, cmd->y, cmd->rx, cmd->ry, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_ELLIPSE: {
        if (size < sizeof(fmrb_link_graphics_ellipse_t)) break;
        const auto *cmd = (const fmrb_link_graphics_ellipse_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->fillEllipse(cmd->x, cmd->y, cmd->rx, cmd->ry, cmd->color);
        return 0;
    }

    // --- Triangles ---

    case FMRB_LINK_GFX_DRAW_TRIANGLE: {
        if (size < sizeof(fmrb_link_graphics_triangle_t)) break;
        const auto *cmd = (const fmrb_link_graphics_triangle_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawTriangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_TRIANGLE: {
        if (size < sizeof(fmrb_link_graphics_triangle_t)) break;
        const auto *cmd = (const fmrb_link_graphics_triangle_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->fillTriangle(cmd->x0, cmd->y0, cmd->x1, cmd->y1, cmd->x2, cmd->y2, cmd->color);
        return 0;
    }

    // --- Arcs ---

    case FMRB_LINK_GFX_DRAW_ARC: {
        if (size < sizeof(fmrb_link_graphics_arc_t)) break;
        const auto *cmd = (const fmrb_link_graphics_arc_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->drawArc(cmd->x, cmd->y, cmd->r0, cmd->r1, cmd->angle0, cmd->angle1, cmd->color);
        return 0;
    }

    case FMRB_LINK_GFX_FILL_ARC: {
        if (size < sizeof(fmrb_link_graphics_arc_t)) break;
        const auto *cmd = (const fmrb_link_graphics_arc_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->fillArc(cmd->x, cmd->y, cmd->r0, cmd->r1, cmd->angle0, cmd->angle1, cmd->color);
        return 0;
    }

    // --- Text ---

    case FMRB_LINK_GFX_DRAW_STRING: {
        if (size < sizeof(fmrb_link_graphics_text_t)) break;
        const auto *cmd = (const fmrb_link_graphics_text_t *)data;
        if (size < sizeof(fmrb_link_graphics_text_t) + cmd->text_len) break;
        const char *text_data = (const char *)(data + sizeof(fmrb_link_graphics_text_t));
        char buf[256];
        size_t len = cmd->text_len < 255u ? cmd->text_len : 255u;
        memcpy(buf, text_data, len);
        buf[len] = '\0';
        auto *s = get_sprite(cmd->canvas_id);
        if (s) {
            if (cmd->bg_transparent) {
                s->setTextColor(cmd->color);
            } else {
                s->setTextColor(cmd->color, cmd->bg_color);
            }
            s->setCursor(cmd->x, cmd->y);
            s->print(buf);
        }
        return 0;
    }

    case FMRB_LINK_GFX_SET_TEXT_SIZE: {
        if (size < sizeof(fmrb_link_graphics_text_size_t)) break;
        const auto *cmd = (const fmrb_link_graphics_text_size_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) s->setTextSize(cmd->size);
        return 0;
    }

    case FMRB_LINK_GFX_SET_TEXT_COLOR: {
        if (size < 4) break;  // canvas_id(2) + fg(1) + bg(1)
        uint16_t canvas_id = *(const uint16_t *)data;
        uint8_t fg = data[2];
        uint8_t bg = data[3];
        auto *s = get_sprite(canvas_id);
        if (s) s->setTextColor(fg, bg);
        return 0;
    }

    case FMRB_LINK_GFX_SET_FONT: {
        if (size < sizeof(fmrb_link_graphics_set_font_t)) break;
        const auto *cmd = (const fmrb_link_graphics_set_font_t *)data;
        auto *s = get_sprite(cmd->canvas_id);
        if (s && cmd->family == FMRB_LINK_GFX_FONT_FAMILY_DEFAULT) {
            s->setFont(&fonts::Font0);
        }
        // JA font support is a future task.
        return 0;
    }

    // --- Canvas management ---

    case FMRB_LINK_GFX_CREATE_CANVAS: {
        if (size < sizeof(fmrb_link_graphics_create_canvas_t)) break;
        const auto *cmd = (const fmrb_link_graphics_create_canvas_t *)data;

        uint16_t cid = g_next_canvas_id++;
        if (cid == DISPLAY_P4_CANVAS_INVALID) cid = g_next_canvas_id++;

        p4_canvas_t *c = canvas_alloc(cid,
                                      (uint16_t)cmd->width, (uint16_t)cmd->height,
                                      cmd->z_order,
                                      cmd->transparent_color,
                                      (bool)cmd->use_transparent);
        if (!c) {
            uint16_t err = DISPLAY_P4_CANVAS_INVALID;
            send_ack(msg_type, seq, (const uint8_t *)&err, sizeof(err));
        } else {
            FMRB_LOGI(TAG, "CREATE_CANVAS: id=%u %dx%d z=%d",
                      cid, (int)cmd->width, (int)cmd->height, cmd->z_order);
            send_ack(msg_type, seq, (const uint8_t *)&cid, sizeof(cid));
        }
        return 1;  // ACK already sent
    }

    case FMRB_LINK_GFX_DELETE_CANVAS: {
        if (size < sizeof(fmrb_link_graphics_delete_canvas_t)) break;
        const auto *cmd = (const fmrb_link_graphics_delete_canvas_t *)data;
        p4_canvas_t *c = canvas_find(cmd->canvas_id);
        if (c) canvas_free(c);
        return 0;
    }

    case FMRB_LINK_GFX_SET_WINDOW_ORDER: {
        if (size < sizeof(fmrb_link_graphics_set_window_order_t)) break;
        const auto *cmd = (const fmrb_link_graphics_set_window_order_t *)data;
        p4_canvas_t *c = canvas_find(cmd->canvas_id);
        if (c) c->z_order = cmd->z_order;
        return 0;
    }

    case FMRB_LINK_GFX_UPDATE_WINDOW: {
        if (size < sizeof(fmrb_link_graphics_update_window_t)) break;
        const auto *cmd = (const fmrb_link_graphics_update_window_t *)data;
        p4_canvas_t *c = canvas_find(cmd->canvas_id);
        if (c) {
            c->push_x = (int16_t)cmd->x;
            c->push_y = (int16_t)cmd->y;
            c->width  = (uint16_t)cmd->width;
            c->height = (uint16_t)cmd->height;
        }
        return 0;
    }

    case FMRB_LINK_GFX_SET_TARGET:
        return 0;  // Not used in sprite-per-canvas approach

    case FMRB_LINK_GFX_PUSH_CANVAS: {
        if (size < sizeof(fmrb_link_graphics_push_canvas_t)) break;
        const auto *cmd = (const fmrb_link_graphics_push_canvas_t *)data;
        p4_canvas_t *src = canvas_find(cmd->canvas_id);
        if (!src) return -1;

        if (cmd->dest_canvas_id == DISPLAY_P4_CANVAS_RENDER ||
            cmd->dest_canvas_id == DISPLAY_P4_CANVAS_SCREEN) {
            src->push_x           = (int16_t)cmd->x;
            src->push_y           = (int16_t)cmd->y;
            src->transparent_color = cmd->transparent_color;
            src->use_transparency  = (bool)cmd->use_transparency;
            src->is_visible        = true;
            render_frame();
        } else {
            // Push canvas-to-canvas
            p4_canvas_t *dst = canvas_find(cmd->dest_canvas_id);
            if (!dst || !src->sprite || !dst->sprite) return -1;
            // Canvas-to-canvas push with direct buffer ops for reliable transparency
            uint16_t *s_buf = (uint16_t *)src->sprite->getBuffer();
            uint16_t *d_buf = (uint16_t *)dst->sprite->getBuffer();
            int s_w = src->sprite->width();
            int s_h = src->sprite->height();
            int d_w = dst->sprite->width();
            int d_h = dst->sprite->height();
            if (s_buf && d_buf) {
                lgfx::rgb332_t tr332; tr332.raw = cmd->transparent_color;
                lgfx::rgb565_t tr_s; tr_s = tr332;
                uint16_t tr565 = tr_s.raw;
                for (int yy = 0; yy < s_h; yy++) {
                    int dy = cmd->y + yy;
                    if (dy < 0 || dy >= d_h) continue;
                    int sx0 = 0, dx = cmd->x;
                    int cw = s_w;
                    if (dx < 0) { sx0 = -dx; cw += dx; dx = 0; }
                    if (dx + cw > d_w) cw = d_w - dx;
                    if (cw <= 0) continue;
                    uint16_t *sr = s_buf + yy * s_w + sx0;
                    uint16_t *dr = d_buf + dy * d_w + dx;
                    if (!cmd->use_transparency) {
                        memcpy(dr, sr, (size_t)cw * 2);
                    } else {
                        for (int xx = 0; xx < cw; xx++) {
                            if (sr[xx] != tr565) dr[xx] = sr[xx];
                        }
                    }
                }
            }
        }
        return 0;
    }

    case FMRB_LINK_GFX_SET_CANVAS_VISIBLE: {
        if (size < sizeof(fmrb_link_graphics_set_canvas_visible_t)) break;
        const auto *cmd = (const fmrb_link_graphics_set_canvas_visible_t *)data;
        p4_canvas_t *c = canvas_find(cmd->canvas_id);
        if (c) c->is_visible = (cmd->visible != 0);
        return 0;
    }

    case FMRB_LINK_GFX_PRESENT:
        render_frame();
        return 0;

    case FMRB_LINK_GFX_GET_PIXEL: {
        if (size < sizeof(fmrb_link_graphics_get_pixel_t)) break;
        const auto *cmd = (const fmrb_link_graphics_get_pixel_t *)data;
        fmrb_link_graphics_pixel_value_t resp = {};
        auto *s = get_sprite(cmd->canvas_id);
        if (s && cmd->x >= 0 && cmd->x < (int16_t)s->width() &&
                 cmd->y >= 0 && cmd->y < (int16_t)s->height()) {
            // Read pixel as RGB888, then convert to RGB332 for kernel
            lgfx::rgb888_t px888 = s->readPixel(cmd->x, cmd->y);
            lgfx::rgb332_t px332;
            px332 = px888;
            resp.color = px332.raw;
            resp.status = 0;
        } else {
            resp.color  = 0;
            resp.status = s ? 1 : 0xFF;
        }
        send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
        return 1;
    }

    // --- Cursor ---

    case FMRB_LINK_GFX_CURSOR_SET_POSITION: {
        if (size < sizeof(fmrb_link_graphics_cursor_position_t)) break;
        const auto *cmd = (const fmrb_link_graphics_cursor_position_t *)data;
        g_cursor_x = cmd->x;
        g_cursor_y = cmd->y;
        return 0;
    }

    case FMRB_LINK_GFX_CURSOR_SET_VISIBLE: {
        if (size < sizeof(fmrb_link_graphics_cursor_visible_t)) break;
        const auto *cmd = (const fmrb_link_graphics_cursor_visible_t *)data;
        g_cursor_visible = cmd->visible;
        return 0;
    }

    // --- GfxBlock VM ---

    case FMRB_LINK_GFX_DEFINE_PROG: {
        if (size < sizeof(fmrb_link_graphics_define_prog_t)) break;
        const auto *cmd = (const fmrb_link_graphics_define_prog_t *)data;
        size_t off = sizeof(fmrb_link_graphics_define_prog_t);
        if (size < off + cmd->bytecode_len + cmd->strtable_len) break;

        const uint8_t *bytecode = data + off;
        const uint8_t *strtable = bytecode + cmd->bytecode_len;
        uint8_t prog_id = display_p4_vm_define_prog(
            cmd->canvas_id, bytecode, cmd->bytecode_len,
            strtable, cmd->strtable_len);
        FMRB_LOGI(TAG, "DEFINE_PROG: canvas=%u bc=%u st=%u -> id=%u",
                  cmd->canvas_id, cmd->bytecode_len, cmd->strtable_len, prog_id);
        send_ack(msg_type, seq, &prog_id, sizeof(prog_id));
        return 1;
    }

    case FMRB_LINK_GFX_EXEC_PROG: {
        if (size < sizeof(fmrb_link_graphics_exec_prog_t)) break;
        const auto *cmd = (const fmrb_link_graphics_exec_prog_t *)data;
        size_t off = sizeof(fmrb_link_graphics_exec_prog_t);
        if (size < off + (size_t)cmd->reg_count * 3) break;
        auto *s = get_sprite(cmd->canvas_id);
        if (s) {
            display_p4_vm_exec_prog(cmd->canvas_id, cmd->prog_id,
                                    data + off, cmd->reg_count, s);
        }
        return 0;
    }

    case FMRB_LINK_GFX_DELETE_PROG: {
        if (size < sizeof(fmrb_link_graphics_delete_prog_t)) break;
        const auto *cmd = (const fmrb_link_graphics_delete_prog_t *)data;
        display_p4_vm_delete_prog(cmd->prog_id);
        return 0;
    }

    // --- Sprite image management ---

    case FMRB_LINK_GFX_CREATE_SPRITE_IMAGE: {
        if (size < sizeof(fmrb_link_graphics_create_sprite_image_t)) break;
        const auto *cmd = (const fmrb_link_graphics_create_sprite_image_t *)data;
        uint16_t img_id = display_p4_sprite_image_create(
            cmd->canvas_id, cmd->width, cmd->height,
            cmd->transparent_color, (bool)cmd->use_transparent);
        FMRB_LOGI(TAG, "CREATE_SPRITE_IMAGE: %ux%u -> id=%u",
                  (unsigned)cmd->width, (unsigned)cmd->height, img_id);
        fmrb_link_graphics_sprite_image_created_t resp = { .image_id = img_id };
        send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
        return 1;
    }

    case FMRB_LINK_GFX_DELETE_SPRITE_IMAGE: {
        if (size < sizeof(fmrb_link_graphics_delete_sprite_image_t)) break;
        const auto *cmd = (const fmrb_link_graphics_delete_sprite_image_t *)data;
        display_p4_sprite_image_delete(cmd->image_id);
        return 0;
    }

    case FMRB_LINK_GFX_SET_SPRITE_IMAGE_TARGET: {
        if (size < sizeof(fmrb_link_graphics_set_sprite_image_target_t)) break;
        const auto *cmd = (const fmrb_link_graphics_set_sprite_image_target_t *)data;
        g_sprite_target_id = cmd->image_id;  // 0 = reset to canvas target
        FMRB_LOGD(TAG, "SET_SPRITE_IMAGE_TARGET: image_id=%u", cmd->image_id);
        return 0;
    }

    case FMRB_LINK_GFX_LOAD_SPRITE_IMAGE_BMP: {
        if (size < sizeof(fmrb_link_graphics_load_sprite_image_bmp_t)) break;
        const auto *cmd = (const fmrb_link_graphics_load_sprite_image_bmp_t *)data;
        if (size < sizeof(*cmd) + cmd->path_len) break;

        // Build VFS path (strip leading '/', prepend /flash/)
        const char *p  = (const char *)(data + sizeof(*cmd));
        int         pl = (int)cmd->path_len;
        if (pl > 0 && p[0] == '/') { p++; pl--; }
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "/flash/%.*s", pl, p);

        LGFX_Sprite *spr = display_p4_sprite_image_get(cmd->image_id);
        if (!spr) {
            FMRB_LOGE(TAG, "LOAD_SPRITE_BMP: image %u not found", cmd->image_id);
            return -1;
        }

        FILE *fp = fopen(full_path, "rb");
        if (!fp) {
            FMRB_LOGE(TAG, "LOAD_SPRITE_BMP: cannot open %s", full_path);
            return -1;
        }
        fseek(fp, 0, SEEK_END);
        long fsz = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (fsz <= 0 || fsz > 65536) {
            FMRB_LOGE(TAG, "LOAD_SPRITE_BMP: invalid file size %ld", fsz);
            fclose(fp);
            return -1;
        }

        uint8_t *bmp_buf = (uint8_t *)fmrb_sys_malloc((size_t)fsz);
        if (!bmp_buf) {
            fclose(fp);
            return -1;
        }
        fread(bmp_buf, 1, (size_t)fsz, fp);
        fclose(fp);

        fmrb_bmp332_t bmp;
        if (fmrb_bmp332_parse(bmp_buf, (size_t)fsz, &bmp) != 0) {
            FMRB_LOGE(TAG, "LOAD_SPRITE_BMP: parse failed: %s", full_path);
            fmrb_sys_free(bmp_buf);
            return -1;
        }

        uint16_t copy_w = (uint16_t)((bmp.width  < (uint16_t)spr->width())  ? bmp.width  : spr->width());
        uint16_t copy_h = (uint16_t)((bmp.height < (uint16_t)spr->height()) ? bmp.height : spr->height());
        for (uint16_t y = 0; y < copy_h; y++) {
            for (uint16_t x = 0; x < copy_w; x++) {
                spr->drawPixel(x, y, bmp.pixels[(size_t)y * bmp.width + x]);
            }
        }
        fmrb_sys_free(bmp_buf);
        FMRB_LOGI(TAG, "LOAD_SPRITE_BMP: loaded %s -> image %u (%ux%u)",
                  full_path, cmd->image_id, bmp.width, bmp.height);
        return 0;
    }

    // --- Sprite instance management ---

    case FMRB_LINK_GFX_CREATE_SPRITE_INSTANCE: {
        if (size < sizeof(fmrb_link_graphics_create_sprite_instance_t)) break;
        const auto *cmd = (const fmrb_link_graphics_create_sprite_instance_t *)data;
        // Copy image_ids to avoid taking address of packed member
        uint16_t local_image_ids[FMRB_SPRITE_MAX_FRAMES];
        memcpy(local_image_ids, cmd->image_ids, sizeof(uint16_t) * cmd->frame_count);
        uint16_t inst_id = display_p4_sprite_instance_create(
            cmd->canvas_id, local_image_ids, cmd->frame_count,
            cmd->x, cmd->y, cmd->z_order);
        FMRB_LOGI(TAG, "CREATE_SPRITE_INSTANCE: canvas=%u frames=%u -> id=%u",
                  cmd->canvas_id, cmd->frame_count, inst_id);
        fmrb_link_graphics_sprite_instance_created_t resp = { .instance_id = inst_id };
        send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
        return 1;
    }

    case FMRB_LINK_GFX_DELETE_SPRITE_INSTANCE: {
        if (size < sizeof(fmrb_link_graphics_delete_sprite_instance_t)) break;
        const auto *cmd = (const fmrb_link_graphics_delete_sprite_instance_t *)data;
        display_p4_sprite_instance_delete(cmd->instance_id);
        return 0;
    }

    case FMRB_LINK_GFX_SPRITE_INSTANCE_MOVE: {
        if (size < sizeof(fmrb_link_graphics_sprite_instance_move_t)) break;
        const auto *cmd = (const fmrb_link_graphics_sprite_instance_move_t *)data;
        display_p4_sprite_instance_move(cmd->instance_id, cmd->x, cmd->y);
        return 0;
    }

    case FMRB_LINK_GFX_SPRITE_INSTANCE_SET_VISIBLE: {
        if (size < sizeof(fmrb_link_graphics_sprite_instance_set_visible_t)) break;
        const auto *cmd = (const fmrb_link_graphics_sprite_instance_set_visible_t *)data;
        display_p4_sprite_instance_set_visible(cmd->instance_id, (bool)cmd->visible);
        return 0;
    }

    case FMRB_LINK_GFX_SPRITE_INSTANCE_SET_FRAME: {
        if (size < sizeof(fmrb_link_graphics_sprite_instance_set_frame_t)) break;
        const auto *cmd = (const fmrb_link_graphics_sprite_instance_set_frame_t *)data;
        display_p4_sprite_instance_set_frame(cmd->instance_id, cmd->frame_index);
        return 0;
    }

    case FMRB_LINK_GFX_DELETE_ALL_SPRITES: {
        if (size < sizeof(fmrb_link_graphics_delete_all_sprites_t)) break;
        const auto *cmd = (const fmrb_link_graphics_delete_all_sprites_t *)data;
        display_p4_sprite_delete_all_for_canvas(cmd->canvas_id);
        return 0;
    }

    // --- File-based image management (PNG → LGFX_Sprite) ---

    case FMRB_LINK_GFX_CREATE_IMAGE_FROM_FILE: {
        if (size < sizeof(fmrb_link_graphics_create_image_from_file_t)) break;
        const auto *cmd = (const fmrb_link_graphics_create_image_from_file_t *)data;
        if (size < sizeof(*cmd) + cmd->path_len) break;

        const char *p  = (const char *)(data + sizeof(*cmd));
        int         pl = (int)cmd->path_len;
        if (pl > 0 && p[0] == '/') { p++; pl--; }
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "/flash/%.*s", pl, p);

        fmrb_link_graphics_image_created_t resp = {};

        // Find free slot
        p4_image_t *slot = nullptr;
        for (int i = 0; i < DISPLAY_P4_MAX_IMAGES; i++) {
            if (!g_images_store[i].in_use) { slot = &g_images_store[i]; break; }
        }
        if (!slot) {
            FMRB_LOGE(TAG, "CREATE_IMAGE_FROM_FILE: image store full");
            send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
            return 1;
        }

        // Read file
        FILE *fp = fopen(full_path, "rb");
        if (!fp) {
            FMRB_LOGE(TAG, "CREATE_IMAGE_FROM_FILE: cannot open %s", full_path);
            send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
            return 1;
        }
        fseek(fp, 0, SEEK_END);
        long fsz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (fsz <= 0 || fsz > 256 * 1024) {
            FMRB_LOGE(TAG, "CREATE_IMAGE_FROM_FILE: bad size %ld", fsz);
            fclose(fp);
            send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
            return 1;
        }

        uint8_t *buf = (uint8_t *)fmrb_sys_malloc((size_t)fsz);
        if (!buf) {
            fclose(fp);
            send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
            return 1;
        }
        fread(buf, 1, (size_t)fsz, fp);
        fclose(fp);

        // Extract image dimensions from PNG IHDR chunk (bytes 16..23)
        int img_w = 0, img_h = 0;
        if (fsz >= 24 && buf[0] == 0x89 && buf[1] == 'P' &&
            buf[2] == 'N' && buf[3] == 'G') {
            img_w = (int)((uint32_t)buf[16] << 24 | (uint32_t)buf[17] << 16 |
                          (uint32_t)buf[18] << 8  | (uint32_t)buf[19]);
            img_h = (int)((uint32_t)buf[20] << 24 | (uint32_t)buf[21] << 16 |
                          (uint32_t)buf[22] << 8  | (uint32_t)buf[23]);
        }
        if (img_w <= 0 || img_h <= 0 || img_w > 2048 || img_h > 2048) {
            FMRB_LOGE(TAG, "CREATE_IMAGE_FROM_FILE: bad PNG dims %dx%d: %s",
                      img_w, img_h, full_path);
            fmrb_sys_free(buf);
            send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
            return 1;
        }

        // Decode PNG into PPA-native RGB565 sprite.
        auto *spr = new LGFX_Sprite();
        spr->setColorDepth(lgfx::rgb565_nonswapped);
        spr->setPsram(true);
        if (!spr->createSprite(img_w, img_h)) {
            FMRB_LOGE(TAG, "CREATE_IMAGE_FROM_FILE: sprite alloc failed %dx%d", img_w, img_h);
            fmrb_sys_free(buf);
            delete spr;
            send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
            return 1;
        }
        spr->fillScreen(0xFFFF);
        spr->drawPng(buf, (size_t)fsz, 0, 0);
        fmrb_sys_free(buf);

        uint16_t id = g_next_image_store_id++;
        if (id == 0) id = g_next_image_store_id++;

        slot->in_use   = true;
        slot->image_id = id;
        slot->sprite   = spr;
        slot->width    = (uint16_t)img_w;
        slot->height   = (uint16_t)img_h;

        resp.image_id = id;
        resp.width    = (uint16_t)img_w;
        resp.height   = (uint16_t)img_h;
        FMRB_LOGI(TAG, "CREATE_IMAGE_FROM_FILE: %s -> id=%u %dx%d",
                  full_path, id, img_w, img_h);
        send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
        return 1;
    }

    case FMRB_LINK_GFX_DELETE_IMAGE: {
        if (size < sizeof(uint16_t)) break;
        uint16_t image_id = *(const uint16_t *)data;
        image_store_destroy(image_store_find(image_id));
        return 0;
    }

    // --- Draw image (PNG image store → canvas) ---
    // DRAW_IMAGE uses PNG image IDs (from CREATE_IMAGE_FROM_FILE).
    // Sprite images use separate commands (DRAW_TILE, sprite compositing).

    case FMRB_LINK_GFX_DRAW_IMAGE: {
        if (size < sizeof(fmrb_link_graphics_draw_image_t)) break;
        const auto *cmd = (const fmrb_link_graphics_draw_image_t *)data;
        auto *dst = get_sprite(cmd->canvas_id);
        if (!dst) {
            FMRB_LOGW(TAG, "DRAW_IMAGE: canvas %u not found", cmd->canvas_id);
            return -1;
        }

        p4_image_t *img = image_store_find(cmd->image_id);
        if (img && img->sprite) {
            img->sprite->pushSprite(dst, cmd->x, cmd->y);
            FMRB_LOGI(TAG, "DRAW_IMAGE: id=%u -> canvas=%u (%d,%d) %ux%u",
                      cmd->image_id, cmd->canvas_id, cmd->x, cmd->y,
                      img->width, img->height);
            return 0;
        }

        FMRB_LOGW(TAG, "DRAW_IMAGE: image %u not found", cmd->image_id);
        return -1;
    }

    case FMRB_LINK_GFX_DRAW_TILE: {
        if (size < sizeof(fmrb_link_graphics_draw_tile_t)) break;
        const auto *cmd = (const fmrb_link_graphics_draw_tile_t *)data;
        LGFX_Sprite *src = display_p4_sprite_image_get(cmd->image_id);
        if (!src) return -1;
        auto *dst = get_sprite(cmd->canvas_id);
        if (!dst) return -1;

        uint8_t trans_color_332 = 0;
        bool use_trans = display_p4_sprite_image_get_transparent(cmd->image_id, &trans_color_332);
        // Convert via LovyanGFX for exact match with sprite buffer content
        lgfx::rgb332_t tc332; tc332.raw = trans_color_332;
        lgfx::rgb565_t tc565; tc565 = tc332;
        uint16_t trans_raw = tc565.raw;
        int sw = src->width();
        int sh = src->height();
        for (int yy = 0; yy < (int)cmd->h; yy++) {
            int sy = cmd->src_y + yy;
            if (sy < 0 || sy >= sh) continue;
            for (int xx = 0; xx < (int)cmd->w; xx++) {
                int sx = cmd->src_x + xx;
                if (sx < 0 || sx >= sw) continue;
                uint16_t pixel = (uint16_t)src->readPixelValue(sx, sy);
                if (use_trans && pixel == trans_raw) continue;
                dst->drawPixel(cmd->dst_x + xx, cmd->dst_y + yy, (lgfx::rgb565_t){pixel});
            }
        }
        return 0;
    }

    // --- Mask operations ---

    case FMRB_LINK_GFX_CREATE_MASK: {
        if (size < sizeof(fmrb_link_graphics_create_mask_t)) break;
        const auto *cmd = (const fmrb_link_graphics_create_mask_t *)data;
        uint16_t mask_id = display_p4_mask_create(cmd->canvas_id, cmd->width, cmd->height);
        fmrb_link_graphics_mask_created_t resp = { .mask_id = mask_id };
        send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
        return 1;
    }

    case FMRB_LINK_GFX_MASK_DATA: {
        if (size < sizeof(fmrb_link_graphics_mask_data_t)) break;
        const auto *cmd = (const fmrb_link_graphics_mask_data_t *)data;
        size_t off = sizeof(fmrb_link_graphics_mask_data_t);
        if (size < off + cmd->chunk_len) break;
        display_p4_mask_data(cmd->mask_id, data + off, cmd->chunk_len, cmd->offset);
        return 0;
    }

    case FMRB_LINK_GFX_DELETE_MASK: {
        if (size < sizeof(fmrb_link_graphics_delete_mask_t)) break;
        const auto *cmd = (const fmrb_link_graphics_delete_mask_t *)data;
        display_p4_mask_delete(cmd->mask_id);
        return 0;
    }

    case FMRB_LINK_GFX_DRAW_IMAGE_MASKED: {
        if (size < sizeof(fmrb_link_graphics_draw_image_masked_t)) break;
        const auto *cmd = (const fmrb_link_graphics_draw_image_masked_t *)data;
        LGFX_Sprite *src = display_p4_sprite_image_get(cmd->image_id);
        if (!src) return -1;
        auto *dst = get_sprite(cmd->canvas_id);
        if (!dst) return -1;
        display_p4_mask_blit(cmd->mask_id, src, dst, cmd->x, cmd->y);
        return 0;
    }

    // --- Blend / output-level (no-op stubs for API completeness) ---

    case FMRB_LINK_GFX_BLEND_RECT:
    case FMRB_LINK_GFX_SET_OUTPUT_LEVEL:
    case FMRB_LINK_GFX_SET_CHROMA_LEVEL:
    case FMRB_LINK_GFX_SET_COMPOSITE_REGIONS:
    case FMRB_LINK_GFX_DRAW_CHAR:
    case FMRB_LINK_GFX_DRAW_BITMAP:
    case FMRB_LINK_GFX_CREATE_IMAGE_FROM_MEM:
        FMRB_LOGD(TAG, "GFX cmd 0x%02x: ACK only (not implemented)", sub_cmd);
        return 0;

    default:
        FMRB_LOGW(TAG, "Unknown GFX cmd: 0x%02x", sub_cmd);
        return -1;
    }

    FMRB_LOGE(TAG, "GFX cmd 0x%02x: payload too small (size=%zu)", sub_cmd, size);
    return -1;
}

// ============================================================
// Message dispatcher
// msgpack format: [type(u8), seq(u8), sub_cmd(u8), payload(bin|nil)]
// ============================================================

static void process_message(const uint8_t *msgpack_data, size_t msgpack_len) {
    msgpack_unpacked msg;
    msgpack_unpacked_init(&msg);

    msgpack_unpack_return ret = msgpack_unpack_next(
        &msg, (const char *)msgpack_data, msgpack_len, NULL);
    if (ret != MSGPACK_UNPACK_SUCCESS) {
        FMRB_LOGE(TAG, "msgpack unpack failed");
        msgpack_unpacked_destroy(&msg);
        return;
    }

    msgpack_object root = msg.data;
    if (root.type != MSGPACK_OBJECT_ARRAY || root.via.array.size < 4) {
        FMRB_LOGE(TAG, "Invalid msgpack format");
        msgpack_unpacked_destroy(&msg);
        return;
    }

    uint8_t type    = (uint8_t)root.via.array.ptr[0].via.u64;
    uint8_t seq     = (uint8_t)root.via.array.ptr[1].via.u64;
    uint8_t sub_cmd = (uint8_t)root.via.array.ptr[2].via.u64;
    uint8_t base_type    = type & FMRB_LINK_TYPE_MASK;
    bool    ack_required = (type & FMRB_LINK_FLAG_ACK_REQUIRED) != 0;

    const uint8_t *payload     = nullptr;
    size_t         payload_len = 0;
    if (root.via.array.ptr[3].type == MSGPACK_OBJECT_BIN) {
        payload     = (const uint8_t *)root.via.array.ptr[3].via.bin.ptr;
        payload_len = root.via.array.ptr[3].via.bin.size;
    }

    switch (base_type) {
    case FMRB_LINK_TYPE_CONTROL:
        if (sub_cmd == FMRB_LINK_CONTROL_VERSION &&
            payload_len >= sizeof(fmrb_control_version_req_t)) {
            uint8_t local_ver = FMRB_LINK_VERSION;
            FMRB_LOGI(TAG, "VERSION check: remote=%u local=%u seq=%u",
                      payload[0], local_ver, seq);
            send_ack(type, seq, &local_ver, sizeof(local_ver));
        } else if (sub_cmd == FMRB_LINK_CONTROL_INIT_DISPLAY &&
                   payload_len >= sizeof(fmrb_control_init_display_t)) {
            const auto *init_cmd = (const fmrb_control_init_display_t *)payload;
            FMRB_LOGI(TAG, "INIT_DISPLAY: %dx%d %d-bit margin=%d,%d",
                      init_cmd->width, init_cmd->height, init_cmd->color_depth,
                      init_cmd->margin_x, init_cmd->margin_y);

            // Allocate cache-aligned RGB565 framebuffer + PPA
            if (!g_framebuffer && init_cmd->width > 0 && init_cmd->height > 0) {
                g_display_width  = init_cmd->width;
                g_display_height = init_cmd->height;

                size_t fb_buf_size = (size_t)g_display_width * g_display_height * 2;
                size_t fb_aligned = 0;
                void *fb_buf = ppa_alloc_buffer(fb_buf_size, &fb_aligned);
                if (!fb_buf) {
                    FMRB_LOGE(TAG, "Framebuffer alloc failed: %dx%d",
                              g_display_width, g_display_height);
                } else {
                    g_framebuffer = new LGFX_Sprite(&g_lcd);
                    // Depth must be set BEFORE setBuffer (see canvas_alloc)
                    set_ppa_native_depth(g_framebuffer);
                    g_framebuffer->setBuffer(fb_buf, g_display_width, g_display_height);
                    g_fb_aligned_size = fb_aligned;
                    FMRB_LOGI(TAG, "Framebuffer allocated: %dx%d RGB565 PPA-native (scale=%dx)",
                              g_display_width, g_display_height, DISPLAY_P4_SCALE_FACTOR);
                }

                // Initialize PPA Blend for canvas compositing
                ppa_client_config_t blend_cfg = {};
                blend_cfg.oper_type = PPA_OPERATION_BLEND;
                if (ppa_register_client(&blend_cfg, &g_ppa_blend) == ESP_OK) {
                    FMRB_LOGI(TAG, "PPA Blend initialized for canvas compositing");
                } else {
                    FMRB_LOGW(TAG, "PPA Blend init failed");
                    g_ppa_blend = NULL;
                }

                // Initialize PPA SRM for hardware 3x scaling
                ppa_client_config_t ppa_cfg = {};
                ppa_cfg.oper_type = PPA_OPERATION_SRM;
                if (ppa_register_client(&ppa_cfg, &g_ppa_srm) == ESP_OK) {
                    int out_w = g_display_width * DISPLAY_P4_SCALE_FACTOR;
                    int out_h = g_display_height * DISPLAY_P4_SCALE_FACTOR;
                    size_t raw_size = (size_t)(out_w * out_h * 2);
                    g_ppa_out_buf = ppa_alloc_buffer(raw_size, &g_ppa_out_buf_size);
                    if (g_ppa_out_buf) {
                        FMRB_LOGI(TAG, "PPA SRM initialized: %dx%d -> %dx%d (%u KB)",
                                  g_display_width, g_display_height,
                                  out_w, out_h,
                                  (unsigned)(g_ppa_out_buf_size / 1024));
                    } else {
                        FMRB_LOGW(TAG, "PPA output buffer alloc failed, using software scaling");
                        ppa_unregister_client(g_ppa_srm);
                        g_ppa_srm = NULL;
                    }
                } else {
                    FMRB_LOGW(TAG, "PPA SRM init failed, using software scaling");
                    g_ppa_srm = NULL;
                }
            }
            send_ack(type, seq, nullptr, 0);
        } else if (sub_cmd == FMRB_LINK_CONTROL_GA_VERSION) {
            fmrb_control_ga_version_resp_t resp = {};
            strncpy(resp.version, FMRB_GA_VERSION, sizeof(resp.version) - 1);
            FMRB_LOGI(TAG, "GA_VERSION: responding with \"%s\"", resp.version);
            send_ack(type, seq, (const uint8_t *)&resp, sizeof(resp));
        } else {
            if (ack_required) send_ack(type, seq, nullptr, 0);
        }
        break;

    case FMRB_LINK_TYPE_GRAPHICS:
        if (payload && payload_len > 0) {
            int result = process_gfx_command(type, sub_cmd, seq, payload, payload_len);
            // 0 = success, no ACK yet; 1 = ACK already sent; -1 = error
            if (result == 0 && ack_required) {
                send_ack(type, seq, nullptr, 0);
            }
        } else if (ack_required) {
            send_ack(type, seq, nullptr, 0);
        }
        break;

    default:
        if (ack_required) send_ack(type, seq, nullptr, 0);
        break;
    }

    msgpack_unpacked_destroy(&msg);
}

// ============================================================
// PPA / LovyanGFX verification test
// Verified on device 2026-07-03: PPA Blend/SRM I/O is little-endian RGB565;
// byte_swap affects input only, output is always non-swapped.
// Re-enable the define below to run the test again (blocks boot ~30s,
// which makes the kernel host-ready wait time out).
// ============================================================
// #define PPA_VERIFICATION_TEST

#ifdef PPA_VERIFICATION_TEST

static void hex_dump(const char *label, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    char line[128];
    int off = snprintf(line, sizeof(line), "%s: ", label);
    for (size_t i = 0; i < len && off < (int)sizeof(line) - 4; i++) {
        off += snprintf(line + off, sizeof(line) - (size_t)off, "%02X ", p[i]);
    }
    FMRB_LOGI(TAG, "%s", line);
}

static void hex_dump_16(const char *label, const uint16_t *buf, size_t count) {
    char line[128];
    int off = snprintf(line, sizeof(line), "%s: ", label);
    for (size_t i = 0; i < count && off < (int)sizeof(line) - 6; i++) {
        off += snprintf(line + off, sizeof(line) - (size_t)off, "%04X ", buf[i]);
    }
    FMRB_LOGI(TAG, "%s", line);
}

static void ppa_verification_test(void) {
    FMRB_LOGI(TAG, "======== PPA VERIFICATION TEST START ========");

    const int SZ = 32;   // small sprite size
    const int SZ2 = 64;  // bg/framebuffer size for blend test

    // ========== Test 1: LovyanGFX color values in both depths ==========
    FMRB_LOGI(TAG, "--- Test 1: RGB565 color values comparison ---");
    {
        // rgb565_2Byte (swapped, depth=16)
        auto *spr_swap = new LGFX_Sprite();
        spr_swap->setColorDepth(16);
        spr_swap->setPsram(false);
        spr_swap->createSprite(4, 4);

        // rgb565_nonswapped (depth=272)
        auto *spr_native = new LGFX_Sprite();
        spr_native->setColorDepth(lgfx::rgb565_nonswapped);
        spr_native->setPsram(false);
        spr_native->createSprite(4, 4);

        // Fill with pure red (R=255, G=0, B=0)
        spr_swap->fillScreen(spr_swap->color888(255, 0, 0));
        spr_native->fillScreen(spr_native->color888(255, 0, 0));

        uint16_t *buf_swap = (uint16_t *)spr_swap->getBuffer();
        uint16_t *buf_native = (uint16_t *)spr_native->getBuffer();

        FMRB_LOGI(TAG, "RED: swap=%04X native=%04X", buf_swap[0], buf_native[0]);
        hex_dump("RED swap bytes", buf_swap, 4);
        hex_dump("RED native bytes", buf_native, 4);

        // Fill with green
        spr_swap->fillScreen(spr_swap->color888(0, 255, 0));
        spr_native->fillScreen(spr_native->color888(0, 255, 0));
        FMRB_LOGI(TAG, "GRN: swap=%04X native=%04X", buf_swap[0], buf_native[0]);
        hex_dump("GRN swap bytes", buf_swap, 4);
        hex_dump("GRN native bytes", buf_native, 4);

        // Fill with blue
        spr_swap->fillScreen(spr_swap->color888(0, 0, 255));
        spr_native->fillScreen(spr_native->color888(0, 0, 255));
        FMRB_LOGI(TAG, "BLU: swap=%04X native=%04X", buf_swap[0], buf_native[0]);
        hex_dump("BLU swap bytes", buf_swap, 4);
        hex_dump("BLU native bytes", buf_native, 4);

        // Fill with white
        spr_swap->fillScreen(spr_swap->color888(255, 255, 255));
        spr_native->fillScreen(spr_native->color888(255, 255, 255));
        FMRB_LOGI(TAG, "WHT: swap=%04X native=%04X", buf_swap[0], buf_native[0]);

        // pushImage test: display both side by side on LCD
        g_lcd.fillScreen(0);
        g_lcd.setTextSize(2);
        g_lcd.setTextColor(0xFFFF);

        // Test pushImage with swap565_t (as LCD expects)
        spr_swap->fillScreen(spr_swap->color888(255, 0, 0));
        g_lcd.pushImage(20, 20, 4, 4, (lgfx::swap565_t *)buf_swap);
        g_lcd.setCursor(20, 30);
        g_lcd.print("swap pushImage(swap565)");

        // Test pushImage with rgb565_t
        spr_native->fillScreen(spr_native->color888(255, 0, 0));
        g_lcd.pushImage(20, 60, 4, 4, (lgfx::rgb565_t *)buf_native);
        g_lcd.setCursor(20, 70);
        g_lcd.print("native pushImage(rgb565)");

        // Test pushSprite for both
        spr_swap->fillScreen(spr_swap->color888(0, 255, 0));
        spr_swap->pushSprite(&g_lcd, 20, 100);
        g_lcd.setCursor(20, 110);
        g_lcd.print("swap pushSprite GREEN");

        spr_native->fillScreen(spr_native->color888(0, 255, 0));
        spr_native->pushSprite(&g_lcd, 20, 140);
        g_lcd.setCursor(20, 150);
        g_lcd.print("native pushSprite GREEN");

        // Cleanup
        spr_swap->deleteSprite(); delete spr_swap;
        spr_native->deleteSprite(); delete spr_native;
    }
    FMRB_LOGI(TAG, "--- Test 1 complete, waiting 5s ---");
    vTaskDelay(pdMS_TO_TICKS(5000));

    // ========== Test 2: PPA SRM with both formats ==========
    FMRB_LOGI(TAG, "--- Test 2: PPA SRM scaling test ---");
    {
        ppa_client_handle_t srm_client = NULL;
        ppa_client_config_t srm_cfg = {};
        srm_cfg.oper_type = PPA_OPERATION_SRM;
        esp_err_t err = ppa_register_client(&srm_cfg, &srm_client);
        FMRB_LOGI(TAG, "PPA SRM register: %d", err);

        if (err == ESP_OK) {
            g_lcd.fillScreen(0);
            g_lcd.setTextSize(2);
            g_lcd.setTextColor(0xFFFF);

            // Test with rgb565_nonswapped input
            size_t in_aligned = 0, out_aligned = 0;
            void *in_buf = ppa_alloc_buffer(SZ * SZ * 2, &in_aligned);
            void *out_buf = ppa_alloc_buffer(SZ * 3 * SZ * 3 * 2, &out_aligned);

            if (in_buf && out_buf) {
                // Fill input with red (nonswapped: R=0xF800)
                uint16_t *in16 = (uint16_t *)in_buf;
                for (int i = 0; i < SZ * SZ; i++) in16[i] = 0xF800; // red nonswapped

                hex_dump_16("SRM in (nonswap red)", in16, 4);

                esp_cache_msync(in_buf, in_aligned, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

                ppa_srm_oper_config_t srm = {};
                srm.in.buffer = in_buf;
                srm.in.pic_w = SZ; srm.in.pic_h = SZ;
                srm.in.block_w = SZ; srm.in.block_h = SZ;
                srm.in.block_offset_x = 0; srm.in.block_offset_y = 0;
                srm.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
                srm.out.buffer = out_buf;
                srm.out.buffer_size = out_aligned;
                srm.out.pic_w = SZ * 3; srm.out.pic_h = SZ * 3;
                srm.out.block_offset_x = 0; srm.out.block_offset_y = 0;
                srm.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
                srm.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
                srm.scale_x = 3.0f; srm.scale_y = 3.0f;
                srm.rgb_swap = false;
                srm.byte_swap = false;
                srm.mode = PPA_TRANS_MODE_BLOCKING;

                err = ppa_do_scale_rotate_mirror(srm_client, &srm);
                FMRB_LOGI(TAG, "SRM (byte_swap=false): %d", err);

                esp_cache_msync(out_buf, out_aligned,
                                ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

                uint16_t *out16 = (uint16_t *)out_buf;
                hex_dump_16("SRM out (byte_swap=false)", out16, 4);
                FMRB_LOGI(TAG, "SRM in[0]=%04X -> out[0]=%04X (same=%s)",
                          in16[0], out16[0], in16[0] == out16[0] ? "YES" : "NO");

                // Display with pushImage as rgb565_t
                g_lcd.pushImage(20, 20, SZ * 3, SZ * 3, (lgfx::rgb565_t *)out_buf);
                g_lcd.setCursor(20 + SZ * 3 + 10, 20);
                g_lcd.print("SRM bs=0");
                g_lcd.setCursor(20 + SZ * 3 + 10, 40);
                g_lcd.printf("in=%04X out=%04X", in16[0], out16[0]);

                // Also try with byte_swap=true
                memset(out_buf, 0, out_aligned);
                esp_cache_msync(out_buf, out_aligned, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

                srm.byte_swap = true;
                err = ppa_do_scale_rotate_mirror(srm_client, &srm);
                FMRB_LOGI(TAG, "SRM (byte_swap=true): %d", err);

                esp_cache_msync(out_buf, out_aligned,
                                ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

                hex_dump_16("SRM out (byte_swap=true)", out16, 4);
                FMRB_LOGI(TAG, "SRM in[0]=%04X -> out[0]=%04X (swapped=%s)",
                          in16[0], out16[0],
                          in16[0] == __builtin_bswap16(out16[0]) ? "YES" : "NO");

                g_lcd.pushImage(20, 140, SZ * 3, SZ * 3, (lgfx::rgb565_t *)out_buf);
                g_lcd.setCursor(20 + SZ * 3 + 10, 140);
                g_lcd.print("SRM bs=1");
                g_lcd.setCursor(20 + SZ * 3 + 10, 160);
                g_lcd.printf("in=%04X out=%04X", in16[0], out16[0]);

                // Also try rgb_swap
                memset(out_buf, 0, out_aligned);
                esp_cache_msync(out_buf, out_aligned, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
                srm.byte_swap = false;
                srm.rgb_swap = true;
                err = ppa_do_scale_rotate_mirror(srm_client, &srm);
                esp_cache_msync(out_buf, out_aligned,
                                ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
                FMRB_LOGI(TAG, "SRM (rgb_swap=true): %d, in=%04X -> out=%04X",
                          err, in16[0], out16[0]);

                g_lcd.pushImage(20, 360, SZ * 3, SZ * 3, (lgfx::rgb565_t *)out_buf);
                g_lcd.setCursor(20 + SZ * 3 + 10, 360);
                g_lcd.print("SRM rgb_swap=1");
                g_lcd.setCursor(20 + SZ * 3 + 10, 380);
                g_lcd.printf("in=%04X out=%04X", in16[0], out16[0]);
            }

            if (in_buf) heap_caps_free(in_buf);
            if (out_buf) heap_caps_free(out_buf);
            ppa_unregister_client(srm_client);
        }
    }
    FMRB_LOGI(TAG, "--- Test 2 complete, waiting 5s ---");
    vTaskDelay(pdMS_TO_TICKS(5000));

    // ========== Test 3: PPA Blend test ==========
    FMRB_LOGI(TAG, "--- Test 3: PPA Blend compositing test ---");
    {
        ppa_client_handle_t blend_client = NULL;
        ppa_client_config_t blend_cfg = {};
        blend_cfg.oper_type = PPA_OPERATION_BLEND;
        esp_err_t err = ppa_register_client(&blend_cfg, &blend_client);
        FMRB_LOGI(TAG, "PPA Blend register: %d", err);

        if (err == ESP_OK) {
            g_lcd.fillScreen(0);
            g_lcd.setTextSize(2);
            g_lcd.setTextColor(0xFFFF);

            size_t fg_aligned = 0, bg_aligned = 0;
            void *fg_buf = ppa_alloc_buffer(SZ * SZ * 2, &fg_aligned);
            void *bg_buf = ppa_alloc_buffer(SZ2 * SZ2 * 2, &bg_aligned);

            if (fg_buf && bg_buf) {
                uint16_t *fg16 = (uint16_t *)fg_buf;
                uint16_t *bg16 = (uint16_t *)bg_buf;

                // FG: red, BG: blue (nonswapped values)
                for (int i = 0; i < SZ * SZ; i++) fg16[i] = 0xF800;
                for (int i = 0; i < SZ2 * SZ2; i++) bg16[i] = 0x001F;

                hex_dump_16("Blend FG (red)", fg16, 2);
                hex_dump_16("Blend BG (blue)", bg16, 2);

                esp_cache_msync(fg_buf, fg_aligned, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
                esp_cache_msync(bg_buf, bg_aligned, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

                ppa_blend_oper_config_t blend = {};
                blend.in_bg.buffer = bg_buf;
                blend.in_bg.pic_w = SZ2; blend.in_bg.pic_h = SZ2;
                blend.in_bg.block_w = SZ; blend.in_bg.block_h = SZ;
                blend.in_bg.block_offset_x = 16; blend.in_bg.block_offset_y = 16;
                blend.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;

                blend.in_fg.buffer = fg_buf;
                blend.in_fg.pic_w = SZ; blend.in_fg.pic_h = SZ;
                blend.in_fg.block_w = SZ; blend.in_fg.block_h = SZ;
                blend.in_fg.block_offset_x = 0; blend.in_fg.block_offset_y = 0;
                blend.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;

                blend.out.buffer = bg_buf;
                blend.out.buffer_size = bg_aligned;
                blend.out.pic_w = SZ2; blend.out.pic_h = SZ2;
                blend.out.block_offset_x = 16; blend.out.block_offset_y = 16;
                blend.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;

                blend.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
                blend.fg_alpha_fix_val = 255;
                blend.bg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;

                // Test A: byte_swap=false
                blend.fg_byte_swap = false;
                blend.bg_byte_swap = false;
                blend.mode = PPA_TRANS_MODE_BLOCKING;

                err = ppa_do_blend(blend_client, &blend);
                FMRB_LOGI(TAG, "Blend (byte_swap=false): %d", err);

                esp_cache_msync(bg_buf, bg_aligned,
                                ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

                // Check: bg[0] should be blue (untouched), bg[16*64+16] should be red (blended)
                FMRB_LOGI(TAG, "Blend out: bg[0,0]=%04X (expect blue 001F), bg[16,16]=%04X (expect red F800)",
                          bg16[0], bg16[16 * SZ2 + 16]);
                hex_dump_16("Blend row0", bg16, 8);
                hex_dump_16("Blend row16", &bg16[16 * SZ2], 24);

                g_lcd.pushImage(20, 20, SZ2, SZ2, (lgfx::rgb565_t *)bg_buf);
                g_lcd.setCursor(20 + SZ2 + 10, 20);
                g_lcd.print("Blend bs=0");
                g_lcd.setCursor(20 + SZ2 + 10, 40);
                g_lcd.printf("[0,0]=%04X", bg16[0]);
                g_lcd.setCursor(20 + SZ2 + 10, 60);
                g_lcd.printf("[16,16]=%04X", bg16[16 * SZ2 + 16]);

                // Test B: reset BG, try byte_swap=true
                for (int i = 0; i < SZ2 * SZ2; i++) bg16[i] = 0x001F;
                esp_cache_msync(fg_buf, fg_aligned, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
                esp_cache_msync(bg_buf, bg_aligned, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

                blend.fg_byte_swap = true;
                blend.bg_byte_swap = true;

                err = ppa_do_blend(blend_client, &blend);
                FMRB_LOGI(TAG, "Blend (byte_swap=true): %d", err);

                esp_cache_msync(bg_buf, bg_aligned,
                                ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

                FMRB_LOGI(TAG, "Blend out bs=1: bg[0,0]=%04X, bg[16,16]=%04X",
                          bg16[0], bg16[16 * SZ2 + 16]);
                hex_dump_16("Blend bs=1 row16", &bg16[16 * SZ2], 24);

                g_lcd.pushImage(20, 120, SZ2, SZ2, (lgfx::rgb565_t *)bg_buf);
                g_lcd.setCursor(20 + SZ2 + 10, 120);
                g_lcd.print("Blend bs=1");
                g_lcd.setCursor(20 + SZ2 + 10, 140);
                g_lcd.printf("[0,0]=%04X", bg16[0]);
                g_lcd.setCursor(20 + SZ2 + 10, 160);
                g_lcd.printf("[16,16]=%04X", bg16[16 * SZ2 + 16]);

                // Test C: input as swap565 (byte_swap=true to unswap for PPA)
                // This simulates the original code path: sprites in swap565,
                // PPA byte_swap=true to read them correctly
                for (int i = 0; i < SZ * SZ; i++) fg16[i] = 0x00F8;  // red in swap565
                for (int i = 0; i < SZ2 * SZ2; i++) bg16[i] = 0x1F00; // blue in swap565
                esp_cache_msync(fg_buf, fg_aligned, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
                esp_cache_msync(bg_buf, bg_aligned, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

                blend.fg_byte_swap = true;
                blend.bg_byte_swap = true;

                err = ppa_do_blend(blend_client, &blend);
                FMRB_LOGI(TAG, "Blend (swap565 input, byte_swap=true): %d", err);

                esp_cache_msync(bg_buf, bg_aligned,
                                ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

                FMRB_LOGI(TAG, "Blend out swap: bg[0,0]=%04X, bg[16,16]=%04X",
                          bg16[0], bg16[16 * SZ2 + 16]);

                g_lcd.pushImage(20, 220, SZ2, SZ2, (lgfx::swap565_t *)bg_buf);
                g_lcd.setCursor(20 + SZ2 + 10, 220);
                g_lcd.print("swap565 in bs=1");
                g_lcd.setCursor(20 + SZ2 + 10, 240);
                g_lcd.printf("[0,0]=%04X", bg16[0]);
                g_lcd.setCursor(20 + SZ2 + 10, 260);
                g_lcd.printf("[16,16]=%04X", bg16[16 * SZ2 + 16]);

                // pushImage as swap565_t for this test
                g_lcd.pushImage(20, 320, SZ2, SZ2, (lgfx::rgb565_t *)bg_buf);
                g_lcd.setCursor(20 + SZ2 + 10, 320);
                g_lcd.print("same as rgb565_t");
            }

            if (fg_buf) heap_caps_free(fg_buf);
            if (bg_buf) heap_caps_free(bg_buf);
            ppa_unregister_client(blend_client);
        }
    }
    FMRB_LOGI(TAG, "--- Test 3 complete, waiting 5s ---");
    vTaskDelay(pdMS_TO_TICKS(5000));

    // ========== Test 4: Full pipeline Blend -> SRM -> pushImage ==========
    FMRB_LOGI(TAG, "--- Test 4: Full pipeline Blend -> SRM -> LCD ---");
    {
        ppa_client_handle_t blend_client = NULL, srm_client = NULL;
        ppa_client_config_t bcfg = {}, scfg = {};
        bcfg.oper_type = PPA_OPERATION_BLEND;
        scfg.oper_type = PPA_OPERATION_SRM;
        esp_err_t e1 = ppa_register_client(&bcfg, &blend_client);
        esp_err_t e2 = ppa_register_client(&scfg, &srm_client);

        if (e1 == ESP_OK && e2 == ESP_OK) {
            g_lcd.fillScreen(0);
            g_lcd.setTextSize(2);
            g_lcd.setTextColor(0xFFFF);

            size_t fg_al = 0, fb_al = 0, out_al = 0;
            void *fg_buf = ppa_alloc_buffer(SZ * SZ * 2, &fg_al);
            void *fb_buf = ppa_alloc_buffer(SZ2 * SZ2 * 2, &fb_al);
            void *out_buf = ppa_alloc_buffer(SZ2 * 3 * SZ2 * 3 * 2, &out_al);

            if (fg_buf && fb_buf && out_buf) {
                uint16_t *fg16 = (uint16_t *)fg_buf;
                uint16_t *fb16 = (uint16_t *)fb_buf;

                // Pattern A: nonswapped, byte_swap=false
                for (int i = 0; i < SZ * SZ; i++) fg16[i] = 0xF800; // red
                for (int i = 0; i < SZ2 * SZ2; i++) fb16[i] = 0x001F; // blue

                esp_cache_msync(fg_buf, fg_al, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
                esp_cache_msync(fb_buf, fb_al, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

                // Blend
                ppa_blend_oper_config_t blend = {};
                blend.in_bg.buffer = fb_buf;
                blend.in_bg.pic_w = SZ2; blend.in_bg.pic_h = SZ2;
                blend.in_bg.block_w = SZ; blend.in_bg.block_h = SZ;
                blend.in_bg.block_offset_x = 16; blend.in_bg.block_offset_y = 16;
                blend.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;
                blend.in_fg.buffer = fg_buf;
                blend.in_fg.pic_w = SZ; blend.in_fg.pic_h = SZ;
                blend.in_fg.block_w = SZ; blend.in_fg.block_h = SZ;
                blend.in_fg.block_offset_x = 0; blend.in_fg.block_offset_y = 0;
                blend.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;
                blend.out.buffer = fb_buf;
                blend.out.buffer_size = fb_al;
                blend.out.pic_w = SZ2; blend.out.pic_h = SZ2;
                blend.out.block_offset_x = 16; blend.out.block_offset_y = 16;
                blend.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;
                blend.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
                blend.fg_alpha_fix_val = 255;
                blend.bg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
                blend.fg_byte_swap = false;
                blend.bg_byte_swap = false;
                blend.mode = PPA_TRANS_MODE_BLOCKING;

                esp_err_t err = ppa_do_blend(blend_client, &blend);
                esp_cache_msync(fb_buf, fb_al,
                                ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

                FMRB_LOGI(TAG, "Pipeline A (nonswap,bs=0): blend=%d fb[0]=%04X fb[16,16]=%04X",
                          err, fb16[0], fb16[16 * SZ2 + 16]);

                // SRM 3x
                esp_cache_msync(fb_buf, fb_al, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
                ppa_srm_oper_config_t srm = {};
                srm.in.buffer = fb_buf;
                srm.in.pic_w = SZ2; srm.in.pic_h = SZ2;
                srm.in.block_w = SZ2; srm.in.block_h = SZ2;
                srm.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
                srm.out.buffer = out_buf;
                srm.out.buffer_size = out_al;
                srm.out.pic_w = SZ2 * 3; srm.out.pic_h = SZ2 * 3;
                srm.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
                srm.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
                srm.scale_x = 3.0f; srm.scale_y = 3.0f;
                srm.byte_swap = false;
                srm.mode = PPA_TRANS_MODE_BLOCKING;

                err = ppa_do_scale_rotate_mirror(srm_client, &srm);
                esp_cache_msync(out_buf, out_al,
                                ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

                uint16_t *out16 = (uint16_t *)out_buf;
                FMRB_LOGI(TAG, "Pipeline A SRM: %d, out[0]=%04X", err, out16[0]);

                g_lcd.pushImage(20, 20, SZ2 * 3, SZ2 * 3, (lgfx::rgb565_t *)out_buf);
                g_lcd.setCursor(20 + SZ2 * 3 + 10, 20);
                g_lcd.print("A: nonswap bs=0");

                // Pattern B: swap565, byte_swap=true (original approach)
                for (int i = 0; i < SZ * SZ; i++) fg16[i] = 0x00F8; // red swap565
                for (int i = 0; i < SZ2 * SZ2; i++) fb16[i] = 0x1F00; // blue swap565

                esp_cache_msync(fg_buf, fg_al, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
                esp_cache_msync(fb_buf, fb_al, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

                blend.fg_byte_swap = true;
                blend.bg_byte_swap = true;
                err = ppa_do_blend(blend_client, &blend);
                esp_cache_msync(fb_buf, fb_al,
                                ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

                FMRB_LOGI(TAG, "Pipeline B (swap565,bs=1): blend=%d fb[0]=%04X fb[16,16]=%04X",
                          err, fb16[0], fb16[16 * SZ2 + 16]);

                esp_cache_msync(fb_buf, fb_al, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
                srm.byte_swap = true;
                err = ppa_do_scale_rotate_mirror(srm_client, &srm);
                esp_cache_msync(out_buf, out_al,
                                ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

                FMRB_LOGI(TAG, "Pipeline B SRM: %d, out[0]=%04X", err, out16[0]);

                g_lcd.pushImage(20, 240, SZ2 * 3, SZ2 * 3, (lgfx::swap565_t *)out_buf);
                g_lcd.setCursor(20 + SZ2 * 3 + 10, 240);
                g_lcd.print("B: swap bs=1");
            }

            if (fg_buf) heap_caps_free(fg_buf);
            if (fb_buf) heap_caps_free(fb_buf);
            if (out_buf) heap_caps_free(out_buf);
            if (blend_client) ppa_unregister_client(blend_client);
            if (srm_client) ppa_unregister_client(srm_client);
        }
    }
    FMRB_LOGI(TAG, "--- Test 4 complete, waiting 10s ---");
    vTaskDelay(pdMS_TO_TICKS(10000));

    FMRB_LOGI(TAG, "======== PPA VERIFICATION TEST END ========");
}

#endif // PPA_VERIFICATION_TEST

// ============================================================
// Main task
// ============================================================

static void display_p4_task(void *arg) {
    (void)arg;
    FMRB_LOGI(TAG, "Tab5 display: power on");
    tab5_power_on();

    FMRB_LOGI(TAG, "Tab5 display: VM + cursor init");
    display_p4_vm_init();
    cursor_init();

    FMRB_LOGI(TAG, "Tab5 display: LGFX init");
    if (!g_lcd.init()) {
        FMRB_LOGE(TAG, "LGFX init failed");
    } else {
        g_lcd.setRotation(3); // landscape: 1280x720 (native portrait 720x1280 rotated 90deg CCW)
        FMRB_LOGI(TAG, "LGFX init OK (%dx%d)", g_lcd.width(), g_lcd.height());
        g_lcd.fillScreen(g_lcd.color888(0, 0, 64));
        g_lcd.setTextColor(g_lcd.color888(255, 255, 255));
        g_lcd.setTextSize(4);
        g_lcd.setCursor(20, 20);
        g_lcd.print("Family mruby");
        g_lcd.setCursor(20, 80);
        g_lcd.print("Modern (P4)");
        g_lcd.setTextSize(2);
        g_lcd.setCursor(20, 160);
        g_lcd.print("Initializing...");

        // Framebuffer is allocated later in INIT_DISPLAY when the kernel
        // reports the actual display_width x display_height.
        g_lcd_ready = true;

#ifdef PPA_VERIFICATION_TEST
        ppa_verification_test();
#endif
    }

    FMRB_LOGI(TAG, "Tab5 display: entering command receive loop");

    while (1) {
        fmrb_link_message_t msg = {
            .data = g_recv_buf,
            .size = sizeof(g_recv_buf),
        };
        fmrb_err_t err = fmrb_hal_link_local_receive_cmd(
            FMRB_LINK_CHANNEL_DEFAULT, &msg, 100);
        if (err == FMRB_OK && msg.size > 0) {
            process_message(g_recv_buf, msg.size);
        }
    }
}

extern "C" fmrb_err_t display_p4_task_init(void) {
    BaseType_t ok = xTaskCreatePinnedToCore(display_p4_task, "display_p4",
                                            16384, NULL, 5, NULL, 1);
    if (ok != pdPASS) {
        FMRB_LOGE(TAG, "Failed to create display_p4 task");
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

extern "C" fmrb_err_t display_p4_task_deinit(void) {
    return FMRB_OK;
}

extern "C" bool display_p4_is_ready(void) {
    return g_lcd_ready;
}

extern "C" int display_p4_get_touch(int16_t *out_x, int16_t *out_y) {
    if (!g_lcd_ready) return 0;
    lgfx::touch_point_t tp;
    int count = g_lcd.getTouch(&tp, 1);
    if (count > 0) {
        *out_x = tp.x;
        *out_y = tp.y;
    }
    return count;
}
