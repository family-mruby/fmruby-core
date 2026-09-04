// Family mruby Modern (ESP32-P4 / M5Stack Tab5) display task.
//
// Receives msgpack-encoded GFX/CONTROL commands from the hal_link_local TX
// buffer, dispatches them, and sends ACK responses via the RX buffer.
//
// All internal buffers use RGB565 (16bpp) for PPA hardware acceleration.
// GFX commands carry RGB332 color values which are converted to RGB565
// at the command dispatch layer.

#include "display_p4_task.h"
#include "display_backend.h"
#include "display_p4_vm.h"
#include "display_p4_sprite.h"
#include "display_p4_video.h"
#ifndef FMRB_PLATFORM_WASM
#if defined(FMRB_HW_NARYAV4)
#include "lgfx_naryav4.hpp"
#else
#include "lgfx_tab5.hpp"
#endif
#else
#include <M5GFX.h>
#endif
#include "fonts/misaki/lgfx_misaki_fonts.hpp"
#include "../audio_p4/audio_p4.h"

#include "fmrb_log.h"
#include "fmrb_hal_link.h"
#include "fmrb_hal_file.h"
#include "fmrb_link_protocol.h"
#include "fmrb_link_types.h"
#include "fmrb_mem.h"
#include "fmrb.h"
#include "fmrb_task_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#ifndef FMRB_PLATFORM_WASM
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ppa.h"
#endif
#include "esp_heap_caps.h"
#include "fmrb_hal_time.h"

extern "C" {
#include "fmrb_bmp332.h"
#include "rd_encoder_jpeg.h"   // EXPORT_FRAME writes a JPEG with it
}

#include <msgpack.h>
#include <cstring>
#include <cstdlib>  // qsort
#ifndef FMRB_PLATFORM_WASM
#include "esp_private/esp_cache_private.h"
#include "esp_cache.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#endif

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
// Cache line size of the PSRAM/DMA region (queried once in ppa_alloc_buffer;
// used for partial esp_cache_msync ranges in render_frame).
static size_t g_cache_line_size = 64;

static void* ppa_alloc_buffer(size_t length, size_t *out_aligned_size) {
    size_t cache_line_size = 64;
#ifndef FMRB_PLATFORM_WASM
    esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &cache_line_size);
#endif
    g_cache_line_size = cache_line_size;
    size_t aligned = (length + cache_line_size - 1) & ~(cache_line_size - 1);
    void *buf = heap_caps_aligned_alloc(cache_line_size, aligned,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf) memset(buf, 0, aligned);
    if (out_aligned_size) *out_aligned_size = buf ? aligned : 0;
    return buf;
}

#if !defined(FMRB_HW_NARYAV4)
// Tab5 board pins / I2C (see fmrb_pin_assign.h; kept local to the driver).
#define TAB5_I2C_PORT   I2C_NUM_1
#define TAB5_I2C_SDA    GPIO_NUM_31
#define TAB5_I2C_SCL    GPIO_NUM_32
#define TAB5_TP_INT     GPIO_NUM_23
#define ST_TOUCH_ADDR   0x55   // ST7121/ST7123 integrated touch controller
#define GT911_TOUCH_ADDR 0x14  // GT911 touch (ILI9881C units), TP INT high at reset
#define PI4IO1_ADDR     0x43   // PI4IO GPIO expander #1 (LCD/touch reset)
#define PI4IO2_ADDR     0x44   // PI4IO GPIO expander #2 (power rails)

// PI4IO #1 runtime bits: P1 = speaker amp enable, P7 = headphone detect
#define PI4IO_REG_OUT_SET   0x05
#define PI4IO_REG_IN_STA    0x0F
#define PI4IO_BIT_SPK_EN    (1 << 1)
#define PI4IO_BIT_HP_DETECT (1 << 7)
#define PI4IO_I2C_FREQ      400000
#endif /* !FMRB_HW_NARYAV4 */

#ifdef FMRB_PLATFORM_WASM
// Sprite parent only: no panel behind it, and nothing here may draw on it.
static lgfx::LGFX_Device g_lcd;
#elif defined(FMRB_HW_NARYAV4)
static LGFX_Naryav4 g_lcd;
#else
static LGFX_Tab5 g_lcd;
#endif
static volatile bool g_lcd_ready = false;

// The panel is Tab5 hardware, not PPA hardware: both output backends drive the
// same one, and so do the parts of this file that draw straight to it (the boot
// screen, the canvas sprites). Hence it stays here and the backends ask for it.
// The same goes for the power-on sequence, the I2C service and headphone
// detection further down -- putting those behind the output interface would
// leave the CPU backend with no lit panel to draw on.
LGFX_Device *display_p4_lcd(void) { return &g_lcd; }
#ifdef FMRB_PLATFORM_WASM
void *display_p4_panel_framebuffer(void) { return NULL; }
#else
void *display_p4_panel_framebuffer(void) { return g_lcd.getFrameBuffer(); }
#endif

#if defined(FMRB_HW_NARYAV4) && !defined(FMRB_PLATFORM_WASM)
// The board's one I2C bus, created by the display because the HDMI bridge is
// the first thing on it that has to answer. Everything else on the bus (the
// codec, mruby app transactions) borrows the handle through the service
// further down rather than making a second bus on the same pins.
static i2c_master_bus_handle_t g_naryav4_i2c = nullptr;
#endif

// Serializes every runtime access to the shared I2C controller (GT911
// touch reads, PI4IO, ES8388, mruby-app transactions). LovyanGFX drives
// this controller at register level, so all runtime traffic must go
// through lgfx's I2C path AND be mutually excluded; this mutex is the
// single gate (see doc/reference/tab5_i2c_bus_notes.md).
static SemaphoreHandle_t g_i2c_mutex = NULL;

// Receive buffer for local link commands
#define DISPLAY_P4_RECV_BUF_SIZE 4096
static uint8_t g_recv_buf[DISPLAY_P4_RECV_BUF_SIZE];

// ============================================================
// Canvas management
// Each canvas is an 8bpp (RGB332) LGFX_Sprite allocated in PSRAM.
// ============================================================

// One canvas per windowed app plus the desktop, so this is the real ceiling on
// how many app windows can be on screen at once -- FMRB_MAX_APPS above it means
// nothing. Each canvas costs two framebuffer-sized RGB332 buffers of PSRAM
// (~200 KB a piece at 426x240), which is why the device targets stop at 8; the
// web build raises it alongside its app ceiling (wasm/CMakeLists.txt).
#ifndef DISPLAY_P4_MAX_CANVAS
#define DISPLAY_P4_MAX_CANVAS    8
#endif
#define DISPLAY_P4_CANVAS_SCREEN  ((uint16_t)0x0000)
#define DISPLAY_P4_CANVAS_RENDER  ((uint16_t)0xFFF0)
#define DISPLAY_P4_CANVAS_INVALID ((uint16_t)0xFFFF)

typedef struct {
    uint16_t     canvas_id;
    // Two buffers per canvas (commit-on-present, matching graphics-audio on
    // Retro/sim): apps DRAW into `sprite` (working); PRESENT copies it into
    // `render_sprite` (committed); render_frame composites ONLY render_sprite.
    // So a render triggered by any app never composites another app's canvas
    // while it is mid-draw -- the committed copy is always a complete frame
    // (doc/p4_display_flicker/design_committed_canvas.md).
    LGFX_Sprite *sprite;
    LGFX_Sprite *render_sprite;
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
    // Active drawing area: what the app currently draws and what gets
    // composited. The buffer behind it is allocated at framebuffer size (see
    // canvas_alloc), so this can grow at runtime without reallocating.
    uint16_t     width, height;
    uint16_t     alloc_width, alloc_height;
    // Composite source viewport (SET_CANVAS_VIEWPORT). view_w == 0 means no
    // viewport: the full canvas is composited (default). When set, only the
    // (view_x, view_y, view_w, view_h) sub-rect is blended at push_x/push_y,
    // turning a large canvas into a hardware-scrolled surface.
    uint16_t     view_x, view_y;
    uint16_t     view_w, view_h;
    // Sprite compositing clip (SET_SPRITE_CLIP). clip_w == 0 means no clip:
    // sprites are bounded by the canvas footprint only (default). Sprites are
    // composited above everything the canvas drew, so a windowed app sets this
    // to its user area to keep them off the frame it drew itself. The rect is
    // in sprite coordinate space (canvas-local, viewport-relative when a
    // viewport is set) and is intersected with the footprint at render time.
    uint16_t     clip_x, clip_y;
    uint16_t     clip_w, clip_h;
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

// Turn a path as an app wrote it into one the VFS answers to. The commands
// carry a length-counted path, so copy it out and hand it to the HAL resolver
// -- the same one File uses, which is what makes "/mnt/sd/movie/demo.mjpg"
// (what the desktop file selector hands back) reach the card, and a bare
// "logo.png" still land under the flash mount. Resolving it here by hand used
// to miss the aliases the HAL knows about.
static void resolve_vfs_path(const char *p, int pl, char *out, size_t out_len) {
    char vpath[256];
    snprintf(vpath, sizeof(vpath), "%.*s", pl > 0 ? pl : 0, p);
    fmrb_hal_file_resolve_path(vpath, out, out_len);
}

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
/* How far the frame is blown up on the way to the panel. Log line only -- the
 * scaling itself lives in the output backend, which is where the geometry is
 * explained. */
#define DISPLAY_P4_SCALE_TEXT "3x"

// How the composited frame reaches the panel (the 3x scale, the rotation into
// the panel's native portrait orientation, and the DSI framebuffer itself) is
// the output backend's business: display_backend_ppa.cpp.

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

    // Allocate at framebuffer size, not at the requested window size, so a
    // window can grow later (runtime fullscreen switch) without reallocating --
    // the same policy graphics-audio uses on Retro. The active area starts at
    // the requested size and UPDATE_WINDOW moves it within this buffer.
    // Cost: 426x240x2 = ~200 KB of PSRAM per canvas.
    uint16_t alloc_w = (g_display_width  > width)  ? (uint16_t)g_display_width  : width;
    uint16_t alloc_h = (g_display_height > height) ? (uint16_t)g_display_height : height;
    size_t buf_size = (size_t)alloc_w * alloc_h * 2;  // RGB565 = 2 bytes/pixel
    size_t aligned_size = 0;
    void *buf = ppa_alloc_buffer(buf_size, &aligned_size);
    if (!buf) {
        FMRB_LOGE(TAG, "Canvas buffer alloc failed: %dx%d (%u bytes)",
                  (int)alloc_w, (int)alloc_h, (unsigned)buf_size);
        return nullptr;
    }

    auto *sprite = new LGFX_Sprite(&g_lcd);
    // Depth must be set BEFORE setBuffer: LGFX_Sprite::setColorDepth() on a
    // sprite that already has a buffer deletes it and reallocates internally
    // (leaking the aligned PPA buffer and moving pixels to internal RAM).
    set_ppa_native_depth(sprite);
    sprite->setBuffer(buf, width, height);

    // Committed buffer (render_sprite): same size/format as the working sprite;
    // PRESENT copies working -> committed and render_frame composites this one.
    // Same aligned size as `buf`, so buf_aligned_size covers both.
    size_t raligned = 0;
    void *rbuf = ppa_alloc_buffer(buf_size, &raligned);
    if (!rbuf) {
        FMRB_LOGE(TAG, "Canvas render buffer alloc failed: %dx%d", (int)alloc_w, (int)alloc_h);
        sprite->deleteSprite();
        delete sprite;
        heap_caps_free(buf);
        return nullptr;
    }
    auto *rsprite = new LGFX_Sprite(&g_lcd);
    set_ppa_native_depth(rsprite);
    rsprite->setBuffer(rbuf, width, height);
    rsprite->clear(0);   // nothing committed until the first present

    p4_canvas_t *c              = &g_canvases[g_canvas_count++];
    c->canvas_id                = canvas_id;
    c->sprite                   = sprite;
    c->render_sprite            = rsprite;
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
    c->alloc_width              = alloc_w;
    c->alloc_height             = alloc_h;
    c->view_x                   = 0;
    c->view_y                   = 0;
    c->view_w                   = 0;   // 0 = no viewport (full composite)
    c->view_h                   = 0;
    c->clip_x                   = 0;
    c->clip_y                   = 0;
    c->clip_w                   = 0;   // 0 = no sprite clip (canvas footprint)
    c->clip_h                   = 0;
    c->buf_aligned_size         = aligned_size;

    FMRB_LOGI(TAG, "Canvas alloc: id=%u %dx%d (buffer %dx%d) z=%d transp=%u/%u",
              canvas_id, width, height, alloc_w, alloc_h, z_order,
              transparent_color, (uint8_t)use_transparency);
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
    if (c->render_sprite) {
        void *rbuf = c->render_sprite->getBuffer();
        c->render_sprite->deleteSprite();
        delete c->render_sprite;
        c->render_sprite = nullptr;
        if (rbuf) heap_caps_free(rbuf);
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
// Mouse cursor (16x16 sprite, drawn as a small LCD patch on top of
// the scaled output; never baked into the framebuffer)
// ============================================================

#define CURSOR_W 16
#define CURSOR_H 16

static LGFX_Sprite *g_cursor_sprite  = nullptr;
static bool         g_cursor_visible = false;
static int          g_cursor_x = 0;
static int          g_cursor_y = 0;
// Position of the patch currently drawn on the LCD (for erase on move)
static int          g_cursor_drawn_x = 0;
static int          g_cursor_drawn_y = 0;
static bool         g_cursor_drawn   = false;

// Deferred rendering: PRESENT (and composition changes) set g_needs_render; the
// task loop renders when it is set, paced to at most one frame per
// RENDER_MIN_INTERVAL_MS (coalescing bursts, ~30fps cap). This is safe against
// multiple concurrent apps because render_frame composites each canvas's
// COMMITTED buffer (render_sprite), filled only at that canvas's own present --
// a render can never catch another app's canvas mid-draw (that lives in the
// working sprite). Cursor moves bypass this via cursor_overlay_update().
#define RENDER_MIN_INTERVAL_MS 33
static bool     g_needs_render   = false;
static uint32_t g_last_render_ms = 0;

// Render throughput stats (logged every 5 s). File-scope; the render happens in
// one place (the task loop's paced render) and note_render() accounts it there.
static uint32_t g_stat_frames = 0;
static uint32_t g_stat_render_ms_total = 0;
static uint32_t g_stat_render_ms_max = 0;
static uint32_t g_stat_last_ms = 0;

// ============================================================
// Frame capture for the remote desktop stream.
//
// When enabled, render_frame() copies the composited 426x240 RGB565
// framebuffer into a double buffer (~200KB memcpy) and signals the
// semaphore. Single-reader design: while the reader holds the front
// buffer, the writer keeps overwriting the back buffer (frames drop).
// Buffers use ppa_alloc_buffer (cache-line aligned PSRAM), which is
// also valid DMA input for the P4 JPEG/PPA engines downstream.
// ============================================================

static SemaphoreHandle_t g_cap_sem = NULL;          // "new frame" signal
static portMUX_TYPE g_cap_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t  *g_cap_buf[2] = { NULL, NULL };
static size_t    g_cap_buf_size = 0;
static int       g_cap_front = -1;                  // index of newest frame
static int       g_cap_locked_idx = -1;             // slot held by the reader
static uint32_t  g_cap_seq = 0;
static volatile bool g_cap_enabled = false;

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
// Cursor patch drawing (small-region update, carried over from the
// retro/m5gfx drivers). The cursor is NOT part of the framebuffer;
// it is drawn onto the LCD as a 48x48 (16x16 scaled 3x) patch built
// from the framebuffer content, so cursor moves transfer ~4.6 KB
// instead of re-rendering and pushing the whole 1.8 MB screen.
// ============================================================

// Push the framebuffer region under (fx,fy), optionally with the cursor
// composited on top, to the LCD as a scaled patch.
static void cursor_patch(int fx, int fy, bool with_cursor) {
    if (!g_framebuffer || !g_cursor_sprite) return;
    const int fb_w = g_framebuffer->width();
    const int fb_h = g_framebuffer->height();

    // Clip the 16x16 patch to framebuffer bounds
    int x0 = fx, y0 = fy;
    int w = CURSOR_W, h = CURSOR_H;
    int sx = 0, sy = 0;  // offset inside the cursor sprite after clipping
    if (x0 < 0) { sx = -x0; w += x0; x0 = 0; }
    if (y0 < 0) { sy = -y0; h += y0; y0 = 0; }
    if (x0 + w > fb_w) w = fb_w - x0;
    if (y0 + h > fb_h) h = fb_h - y0;
    if (w <= 0 || h <= 0) return;

    const uint16_t *fb  = (const uint16_t *)g_framebuffer->getBuffer();
    const uint16_t *cur = (const uint16_t *)g_cursor_sprite->getBuffer();
    if (!fb || !cur) return;

    // CURSOR_TRANSPARENT (magenta 0xFF00FF) in RGB565 non-swapped
    const uint16_t key = 0xF81F;

    // Composite the cursor over the framebuffer region into a small block; the
    // backend scales that block onto the panel. with_cursor false means "put
    // the framebuffer back", which erases the cursor from its old place.
    static uint16_t block[CURSOR_W * CURSOR_H];
    for (int y = 0; y < h; y++) {
        const uint16_t *fb_row  = fb  + (size_t)(y0 + y) * fb_w + x0;
        const uint16_t *cur_row = cur + (size_t)(sy + y) * CURSOR_W + sx;
        uint16_t *out = block + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            uint16_t px = fb_row[x];
            if (with_cursor && cur_row[x] != key) px = cur_row[x];
            out[x] = px;
        }
    }

    display_backend()->present_patch(block, x0, y0, w, h, fb_w, fb_h);
}

// Erase the previously drawn patch (restore framebuffer content) and
// draw the cursor at its current position. Display task context only.
static void cursor_overlay_update(void) {
    if (g_cursor_drawn &&
        (g_cursor_drawn_x != g_cursor_x || g_cursor_drawn_y != g_cursor_y ||
         !g_cursor_visible)) {
        cursor_patch(g_cursor_drawn_x, g_cursor_drawn_y, false);
        g_cursor_drawn = false;
    }
    if (g_cursor_visible && !g_cursor_drawn) {
        cursor_patch(g_cursor_x, g_cursor_y, true);
        g_cursor_drawn_x = g_cursor_x;
        g_cursor_drawn_y = g_cursor_y;
        g_cursor_drawn   = true;
    }
}

// Bake the cursor into the framebuffer so the scale-out (PPA / software push)
// writes it to the DSI buffer atomically with the frame. Drawing the cursor as
// a separate patch after the scale-out (the old render_frame tail) leaves a
// window in which the live-scanned DSI buffer has no cursor; at high render
// rates the scan catches it and the cursor flickers (doc/p4_display_flicker/).
// The saved region is restored right after the push so the framebuffer stays
// cursor-free for the capture, the fast-path cursor_patch on moves, and the
// next composite. Clipping mirrors cursor_patch. Returns the clipped rect via
// out-params (used by framebuffer_restore_cursor); false if nothing to bake.
static bool framebuffer_bake_cursor(int fx, int fy, uint16_t *save,
                                    int *ox0, int *oy0, int *ow, int *oh) {
    if (!g_framebuffer || !g_cursor_sprite) return false;
    const int fb_w = g_framebuffer->width();
    const int fb_h = g_framebuffer->height();
    int x0 = fx, y0 = fy, w = CURSOR_W, h = CURSOR_H, sx = 0, sy = 0;
    if (x0 < 0) { sx = -x0; w += x0; x0 = 0; }
    if (y0 < 0) { sy = -y0; h += y0; y0 = 0; }
    if (x0 + w > fb_w) w = fb_w - x0;
    if (y0 + h > fb_h) h = fb_h - y0;
    if (w <= 0 || h <= 0) return false;

    uint16_t *fb        = (uint16_t *)g_framebuffer->getBuffer();
    const uint16_t *cur = (const uint16_t *)g_cursor_sprite->getBuffer();
    if (!fb || !cur) return false;
    const uint16_t key = 0xF81F;  // CURSOR_TRANSPARENT in RGB565 non-swapped

    for (int y = 0; y < h; y++) {
        uint16_t *fb_row        = fb   + (size_t)(y0 + y) * fb_w + x0;
        const uint16_t *cur_row = cur  + (size_t)(sy + y) * CURSOR_W + sx;
        uint16_t *sv_row        = save + (size_t)y * CURSOR_W;
        for (int x = 0; x < w; x++) {
            sv_row[x] = fb_row[x];                          // save original
            if (cur_row[x] != key) fb_row[x] = cur_row[x];  // draw cursor pixel
        }
    }
    *ox0 = x0; *oy0 = y0; *ow = w; *oh = h;
    return true;
}

static void framebuffer_restore_cursor(const uint16_t *save,
                                       int x0, int y0, int w, int h) {
    if (!g_framebuffer) return;
    uint16_t *fb = (uint16_t *)g_framebuffer->getBuffer();
    if (!fb) return;
    const int fb_w = g_framebuffer->width();
    for (int y = 0; y < h; y++) {
        uint16_t *fb_row       = fb   + (size_t)(y0 + y) * fb_w + x0;
        const uint16_t *sv_row = save + (size_t)y * CURSOR_W;
        for (int x = 0; x < w; x++) fb_row[x] = sv_row[x];
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

// Blend one source block of a canvas into the framebuffer at (dst_x, dst_y),
// clipping to framebuffer bounds. render_frame composites a normal canvas
// with a single block; a viewport canvas (torus addressing) with up to four.
static void blend_canvas_block(p4_canvas_t *c, int sw, int sh,
                               int src_x, int src_y, int bw, int bh,
                               int dst_x, int dst_y, int fb_w, int fb_h) {
    // Clip the on-screen footprint to framebuffer bounds
    int sx0 = 0, sy0 = 0, dx = dst_x, dy = dst_y;
    int cw = bw, ch = bh;
    if (dx < 0) { sx0 = -dx; cw += dx; dx = 0; }
    if (dy < 0) { sy0 = -dy; ch += dy; dy = 0; }
    if (dx + cw > fb_w) cw = fb_w - dx;
    if (dy + ch > fb_h) ch = fb_h - dy;
    if (cw <= 0 || ch <= 0) return;

    display_blend_req_t req = {};
    // Composite the committed buffer, never the working one (commit-on-present).
    req.fg        = c->render_sprite->getBuffer();
    req.fg_pic_w  = sw;
    req.fg_pic_h  = sh;
    req.src_x     = src_x + sx0;
    req.src_y     = src_y + sy0;
    req.fg_size   = c->buf_aligned_size;

    req.bg        = g_framebuffer->getBuffer();
    req.bg_pic_w  = fb_w;
    req.bg_pic_h  = fb_h;
    req.bg_size   = g_fb_aligned_size;
    req.dst_x     = dx;
    req.dst_y     = dy;

    req.w = cw;
    req.h = ch;

    if (c->create_use_transparency) {
        req.color_key = true;
        req.ck_r_low  = c->create_ck_r_low;
        req.ck_g_low  = c->create_ck_g_low;
        req.ck_b_low  = c->create_ck_b_low;
        req.ck_r_high = c->create_ck_r_high;
        req.ck_g_high = c->create_ck_g_high;
        req.ck_b_high = c->create_ck_b_high;
    }

    req.canvas_id   = c->canvas_id;
    req.is_viewport = (c->view_w > 0);

    display_backend()->blend_block(&req);
}

static void render_frame(void) {
    if (!g_framebuffer || g_canvas_count == 0) return;

    if (g_first_render) {
        display_backend()->first_frame();
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
        if (!c->is_visible || !c->render_sprite) continue;

        // Composite the committed buffer, never the working one (commit-on-present).
        int sw = c->render_sprite->width();
        int sh = c->render_sprite->height();

        if (c->view_w == 0) {
            // Default path: composite the whole canvas at its push position
            blend_canvas_block(c, sw, sh, 0, 0, sw, sh,
                               c->push_x, c->push_y, fb_w, fb_h);
        } else {
            // Viewport path (SET_CANVAS_VIEWPORT): the canvas is a torus.
            // The source rect may wrap around the canvas edges, so the
            // footprint is composited in up to four blocks. This lets a
            // ring-buffer canvas barely larger than the viewport scroll an
            // arbitrarily large world (the app stamps newly exposed tiles
            // on the hidden side as the viewport moves).
            int vx = c->view_x % sw;
            int vy = c->view_y % sh;
            int vw = (c->view_w < sw) ? c->view_w : sw;
            int vh = (c->view_h < sh) ? c->view_h : sh;
            int w1 = sw - vx; if (w1 > vw) w1 = vw;
            int w2 = vw - w1;
            int h1 = sh - vy; if (h1 > vh) h1 = vh;
            int h2 = vh - h1;

            blend_canvas_block(c, sw, sh, vx, vy, w1, h1,
                               c->push_x, c->push_y, fb_w, fb_h);
            if (w2 > 0)
                blend_canvas_block(c, sw, sh, 0, vy, w2, h1,
                                   c->push_x + w1, c->push_y, fb_w, fb_h);
            if (h2 > 0)
                blend_canvas_block(c, sw, sh, vx, 0, w1, h2,
                                   c->push_x, c->push_y + h1, fb_w, fb_h);
            if (w2 > 0 && h2 > 0)
                blend_canvas_block(c, sw, sh, 0, 0, w2, h2,
                                   c->push_x + w1, c->push_y + h1, fb_w, fb_h);
        }

        // Sprites use canvas-local coordinates and must not spill outside
        // the canvas footprint on screen (sprite compositing otherwise
        // clips to framebuffer bounds only, leaking sprites onto the
        // desktop and other windows). For viewport canvases the visible
        // footprint is the viewport instead of the full canvas.
        int dx = c->push_x, dy = c->push_y;
        int cw = sw, ch = sh;
        if (c->view_w > 0) {
            cw = (c->view_w < sw) ? c->view_w : sw;
            ch = (c->view_h < sh) ? c->view_h : sh;
        }
        // SET_SPRITE_CLIP narrows that footprint further: the app confines its
        // sprites to a sub-rect (typically the user area, so they stay off the
        // window frame it drew into the same canvas). The clip shares the
        // sprite coordinate space, i.e. it is relative to the canvas origin on
        // screen, exactly like the composite offset below.
        if (c->clip_w > 0) {
            int sx = c->push_x + c->clip_x;
            int sy = c->push_y + c->clip_y;
            if (sx > dx) { cw -= (sx - dx); dx = sx; }
            if (sy > dy) { ch -= (sy - dy); dy = sy; }
            if (dx + cw > sx + c->clip_w) cw = sx + c->clip_w - dx;
            if (dy + ch > sy + c->clip_h) ch = sy + c->clip_h - dy;
        }
        if (dx < 0) { cw += dx; dx = 0; }
        if (dy < 0) { ch += dy; dy = 0; }
        if (dx + cw > fb_w) cw = fb_w - dx;
        if (dy + ch > fb_h) ch = fb_h - dy;
        if (cw > 0 && ch > 0) {
            g_framebuffer->setClipRect(dx, dy, cw, ch);
            display_p4_sprite_composite(c->canvas_id, g_framebuffer,
                                        c->push_x, c->push_y);
            g_framebuffer->clearClipRect();
        }
    }

    // Remote-desktop capture: grab the composited frame before the SRM
    // scale-out. The writer (this task) never touches the slot locked by
    // the reader; while the reader is slow, frames simply overwrite the
    // other slot and drop.
    if (g_cap_enabled && g_cap_buf[0]) {
        void *fb_ptr = g_framebuffer->getBuffer();
        size_t copy_len = (size_t)g_framebuffer->width()
                        * g_framebuffer->height() * 2;
        if (copy_len > g_cap_buf_size) copy_len = g_cap_buf_size;

        portENTER_CRITICAL(&g_cap_lock);
        int target;
        if (g_cap_locked_idx >= 0)   target = 1 - g_cap_locked_idx;
        else if (g_cap_front >= 0)   target = 1 - g_cap_front;
        else                         target = 0;
        portEXIT_CRITICAL(&g_cap_lock);

        memcpy(g_cap_buf[target], fb_ptr, copy_len);

        portENTER_CRITICAL(&g_cap_lock);
        g_cap_front = target;
        g_cap_seq++;
        portEXIT_CRITICAL(&g_cap_lock);
        xSemaphoreGive(g_cap_sem);
    }

    // Bake the cursor into the framebuffer so the scale-out below writes it to
    // the DSI buffer atomically with the frame (no separate post-scale patch,
    // which would flicker on the live-scanned buffer at high render rates).
    // Restored right after the push so the framebuffer stays cursor-free.
    static uint16_t s_cursor_save[CURSOR_W * CURSOR_H];
    bool cur_baked = false;
    int cur_x0 = 0, cur_y0 = 0, cur_w = 0, cur_h = 0;
    if (g_cursor_visible) {
        cur_baked = framebuffer_bake_cursor(g_cursor_x, g_cursor_y, s_cursor_save,
                                            &cur_x0, &cur_y0, &cur_w, &cur_h);
    }

    // Hand the finished frame to the output backend: 3x scale and rotate onto
    // the panel (PPA SRM on the device, pushRotateZoom in software).
    display_backend()->present(g_framebuffer, g_fb_aligned_size);

    // Restore the framebuffer region under the cursor (the cursor is now on the
    // DSI buffer as part of the pushed frame). Record it as drawn so a later
    // fast-path move erases it from the right place.
    if (cur_baked) {
        framebuffer_restore_cursor(s_cursor_save, cur_x0, cur_y0, cur_w, cur_h);
        g_cursor_drawn   = true;
        g_cursor_drawn_x = g_cursor_x;
        g_cursor_drawn_y = g_cursor_y;
    } else {
        g_cursor_drawn = false;
    }
}

#if !defined(FMRB_PLATFORM_WASM) && !defined(FMRB_HW_NARYAV4)
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

// Determine which panel/touch combination this Tab5 carries, following
// M5GFX's Tab5 detection (managed_components/m5stack__m5gfx/src/M5GFX.cpp).
// The touch controller answers before the DSI link is up, so the variant is
// decided over I2C: the ST7123 touch block (0x55) reports a firmware version
// that tells ST7121 (1) from ST7123 (3), and a GT911 at 0x14 means this is an
// ILI9881C unit. ST7121 and ST7123 differ in DSI lane rate, so the DSI ID
// cannot safely tell them apart -- the touch probe has to come first. The
// model number on the rear label does not decide this either: a unit labelled
// ST7123 was measured reporting touch firmware version 1, i.e. ST7121.
//
// The ST touch block needs a few tens of ms after reset before it answers
// (~50 ms measured by M5GFX), so keep polling until one of the two chips
// responds rather than probing once.
//
// Called with the IDF i2c_master bus still owned by tab5_power_on(), after
// the LCD/touch resets have been released.
static Tab5PanelVariant tab5_probe_panel(i2c_master_bus_handle_t bus) {
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.scl_speed_hz = 100000;
    dev_cfg.device_address = ST_TOUCH_ADDR;
    i2c_master_dev_handle_t st = NULL;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &st) != ESP_OK) {
        FMRB_LOGW(TAG, "panel probe: i2c device add failed, assuming ILI9881C");
        return Tab5PanelVariant::ILI9881C;
    }

    // A NACK is the normal answer here (only one of the two chips exists),
    // and the IDF driver logs every one of them at error level. Quiet it for
    // the duration of the probe.
    fmrb_log_level_set("i2c.master", FMRB_LOG_NONE);

    Tab5PanelVariant variant = Tab5PanelVariant::ILI9881C;
    bool decided = false;
    bool st_answered = false;
    int last_logged_fw = -1;

    for (int i = 0; i < 60 && !decided; ++i) {
        // ST7123 touch firmware version register (0x0000), 16-bit register
        // address, one byte back.
        const uint8_t fw_reg[2] = { 0x00, 0x00 };
        uint8_t fw = 0;
        if (i2c_master_transmit_receive(st, fw_reg, sizeof(fw_reg), &fw, 1, 100) == ESP_OK) {
            st_answered = true;
            if (fw != last_logged_fw) {  // do not repeat the log while retrying
                last_logged_fw = fw;
                FMRB_LOGI(TAG, "panel probe: ST touch FW version %02x", fw);
            }
            if (fw == 1) {
                variant = Tab5PanelVariant::ST7121;
                decided = true;
            } else if (fw == 3) {
                variant = Tab5PanelVariant::ST7123;
                decided = true;
            }
        } else if (i2c_master_probe(bus, GT911_TOUCH_ADDR, 50) == ESP_OK) {
            // Address ACK only: reading a GT911 here would advance its
            // internal pointer.
            variant = Tab5PanelVariant::ILI9881C;
            decided = true;
        }
        if (!decided) vTaskDelay(pdMS_TO_TICKS(10));
    }

    i2c_master_bus_rm_device(st);
    fmrb_log_level_set("i2c.master", FMRB_LOG_ERROR);

    if (!decided) {
        if (st_answered) {
            // An ST touch block with a firmware version we do not know about.
            // ST7123 is the current production part, so prefer it.
            FMRB_LOGW(TAG, "panel probe: unknown ST touch FW %02x, assuming ST7123",
                      last_logged_fw);
            variant = Tab5PanelVariant::ST7123;
        } else {
            // Neither chip answered. ILI9881C is the configuration this
            // firmware has always shipped with, so fall back to it.
            FMRB_LOGW(TAG, "panel probe: no touch controller answered, assuming ILI9881C");
        }
    }

    FMRB_LOGI(TAG, "panel probe: %s",
              variant == Tab5PanelVariant::ST7123 ? "ST7123 (display+touch)"
            : variant == Tab5PanelVariant::ST7121 ? "ST7121 + ST7123 touch"
                                                  : "ILI9881C + GT911 touch");
    return variant;
}

static Tab5PanelVariant tab5_power_on(void) {
    // TP INT high during reset selects GT911 touch I2C address 0x14 (the
    // ST7123 touch block ignores it and always answers at 0x55).
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
        return Tab5PanelVariant::ILI9881C;
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

    // Identify the panel while this bus is still ours; lgfx takes the port
    // over once the devices below are removed.
    Tab5PanelVariant variant = tab5_probe_panel(bus);

    i2c_master_bus_rm_device(io1);
    i2c_master_bus_rm_device(io2);
    i2c_del_master_bus(bus);
    // Backlight: LEDC PWM via Light_PWM in LGFX_Tab5 (GPIO22, ch7, 44100Hz).
    return variant;
}
#endif /* !FMRB_PLATFORM_WASM && !FMRB_HW_NARYAV4 */

// ============================================================
// ACK response sender
// ============================================================

/* Fixed-buffer msgpack sink, so packing an ACK costs no allocation.
   msgpack_sbuffer would malloc and then realloc as it grows, and the packer
   inlines that write callback into this TU, out of reach of the allocator remap
   the msgpack-esp32 component applies to its own sources. Every ACK is a header
   plus a response struct of at most a handful of bytes, so a small stack buffer
   covers all of them; overflow is reported rather than truncated silently. */
struct ack_buf_t {
    uint8_t *buf;
    size_t   cap;
    size_t   len;
    bool     overflow;
};

static int ack_buf_write(void *data, const char *b, size_t l) {
    auto *f = (ack_buf_t *)data;
    if (f->len + l > f->cap) { f->overflow = true; return -1; }
    memcpy(f->buf + f->len, b, l);
    f->len += l;
    return 0;
}

/* array(4) + three small ints + a bin header, then the payload. The largest
   response struct in fmrb_link_protocol.h is 6 bytes, so this has ample slack. */
#define DISPLAY_P4_ACK_BUF_SIZE 64

static void send_ack(uint8_t msg_type, uint8_t seq, const uint8_t *data, size_t data_len) {
    uint8_t  storage[DISPLAY_P4_ACK_BUF_SIZE];
    ack_buf_t sink = { storage, sizeof(storage), 0, false };
    msgpack_packer pk;
    msgpack_packer_init(&pk, &sink, ack_buf_write);

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

    if (sink.overflow) {
        FMRB_LOGE(TAG, "ACK too large to pack: type=0x%02x seq=%u data_len=%zu (cap=%d)",
                  msg_type, seq, data_len, DISPLAY_P4_ACK_BUF_SIZE);
        return;
    }

    fmrb_link_message_t resp = {
        .data = sink.buf,
        .size = sink.len,
    };
    fmrb_hal_link_local_send_response(FMRB_LINK_CHANNEL_DEFAULT, &resp, 1000);
}

// Render a frame that a prior PRESENT (or composition change) left pending,
// before the command stream mutates any canvas. PRESENT sets g_needs_render
// once the frame is complete; rendering here -- at the top of the next command,
// in order -- guarantees the render never runs against a half-drawn next frame.
// A timer-fired render (the old loop) could fire between a frame's clear and
// its bars and composite the blank intermediate (doc/p4_display_flicker/).
// g_needs_render is only set at a frame boundary, so this fires at most once
// per frame (the first command after a present).
// Account one render that started at start_ms and log throughput every 5 s.
static void note_render(uint32_t start_ms) {
    uint32_t render_ms =
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - start_ms;
    g_stat_frames++;
    g_stat_render_ms_total += render_ms;
    if (render_ms > g_stat_render_ms_max) g_stat_render_ms_max = render_ms;
    if ((uint32_t)(start_ms - g_stat_last_ms) >= 5000) {
        FMRB_LOGI(TAG, "render: %lu frames/5s avg=%lums max=%lums",
                  (unsigned long)g_stat_frames,
                  (unsigned long)(g_stat_render_ms_total / g_stat_frames),
                  (unsigned long)g_stat_render_ms_max);
        g_stat_frames = 0;
        g_stat_render_ms_total = 0;
        g_stat_render_ms_max = 0;
        g_stat_last_ms = start_ms;
    }
}

// ============================================================
// GFX command dispatcher
// Returns: 0 = success, no ACK sent
//          1 = ACK with data already sent
//         -1 = error / unimplemented
// ============================================================

// EXPORT_FRAME: write what the last present composited to a JPEG file.
//
// Commands are handled in the task that renders, and a render is deferred and
// paced -- so when this command arrives, right behind the present it belongs
// to, that present has not reached the framebuffer yet. Render it here, out
// of the pacing, and take the pixels from the framebuffer directly: that is
// the same RGB565 the remote-desktop capture copies, before the cursor is
// baked in, so an exported slide carries no mouse pointer.
//
// Synchronous on purpose. The whole encode and write finish before the next
// command is read, so an app can queue present/export per slide and know the
// pictures come out in that order; it learns the last one landed by finding
// the file (core and display share this filesystem).
#define EXPORT_FRAME_QUALITY 90

static bool export_frame_jpeg(const char *path)
{
    if (!g_framebuffer) {
        FMRB_LOGE(TAG, "EXPORT_FRAME: no framebuffer");
        return false;
    }

    if (g_needs_render) {
        g_needs_render = false;
        render_frame();
        g_last_render_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    }

    // The encoder owns one staging and one output buffer, shared with the
    // MJPEG stream. Wait for it rather than corrupt a frame someone is
    // sending; two seconds is many frames at the stream's pace.
    fmrb_err_t err = rd_encoder_jpeg_lock(3000);
    if (err != FMRB_OK) {
        FMRB_LOGE(TAG, "EXPORT_FRAME: encoder busy");
        return false;
    }

    bool ok = false;
    err = rd_encoder_jpeg_init((uint16_t)g_framebuffer->width(),
                               (uint16_t)g_framebuffer->height(),
                               EXPORT_FRAME_QUALITY);
    if (err != FMRB_OK) {
        FMRB_LOGE(TAG, "EXPORT_FRAME: encoder init failed: %d", err);
    } else {
        const uint8_t *jpeg = NULL;
        size_t jpeg_len = 0;
        err = rd_encoder_jpeg_encode_q(
            (const uint16_t *)g_framebuffer->getBuffer(),
            EXPORT_FRAME_QUALITY, &jpeg, &jpeg_len);
        if (err != FMRB_OK) {
            FMRB_LOGE(TAG, "EXPORT_FRAME: encode failed: %d", err);
        } else {
            FILE *fp = fopen(path, "wb");
            if (!fp) {
                FMRB_LOGE(TAG, "EXPORT_FRAME: cannot open %s", path);
            } else {
                size_t wrote = fwrite(jpeg, 1, jpeg_len, fp);
                // A short card write leaves a truncated picture behind, which
                // reads as a corrupt file later; say so now and delete it.
                if (fclose(fp) != 0 || wrote != jpeg_len) {
                    FMRB_LOGE(TAG, "EXPORT_FRAME: short write %u/%u to %s",
                              (unsigned)wrote, (unsigned)jpeg_len, path);
                    remove(path);
                } else {
                    FMRB_LOGI(TAG, "EXPORT_FRAME: wrote %s (%u bytes)",
                              path, (unsigned)jpeg_len);
                    ok = true;
                }
            }
        }
    }
    rd_encoder_jpeg_unlock();
    return ok;
}

static int process_gfx_command(uint8_t msg_type, uint8_t sub_cmd, uint8_t seq,
                                const uint8_t *data, size_t size) {
    switch (sub_cmd) {

    // --- Screen-level fill ---

    case FMRB_LINK_GFX_CLEAR:
    case FMRB_LINK_GFX_FILL_SCREEN: {
        if (size < sizeof(fmrb_link_graphics_clear_t)) break;
        const auto *cmd = (const fmrb_link_graphics_clear_t *)data;
        // Clears the WORKING sprite only; the compositor reads the committed
        // buffer (filled at present), so a clear-then-redraw in progress is never
        // composited half-done -- the quick-tap / mid-burst wallpaper flicker
        // (doc/p4_display_flicker/) cannot recur.
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
            if (cmd->hybrid_mode == 1) {
                // Hybrid mode: render ASCII runs with Font0 and UTF-8
                // multi-byte runs with misaki_8 (same behavior as the
                // WROVER graphics_handler). print() keeps the cursor
                // between calls so runs stitch together naturally.
                const lgfx::IFont *saved_font = s->getFont();
                const uint8_t *p = (const uint8_t *)buf;
                const uint8_t *end = p + len;
                char run_buf[256];
                while (p < end) {
                    const uint8_t *run_start = p;
                    bool is_ascii = (*p < 0x80);
                    if (is_ascii) {
                        while (p < end && *p < 0x80) p++;
                        s->setFont(&fonts::Font0);
                    } else {
                        while (p < end && *p >= 0x80) p++;
                        s->setFont(&fonts::misaki_8);
                    }
                    size_t run_len = (size_t)(p - run_start);
                    if (run_len >= sizeof(run_buf)) run_len = sizeof(run_buf) - 1;
                    memcpy(run_buf, run_start, run_len);
                    run_buf[run_len] = '\0';
                    s->print(run_buf);
                }
                // Restore the caller's font selection so subsequent draws
                // (and the Ruby-side font cache) stay consistent.
                s->setFont(saved_font);
            } else {
                s->print(buf);
            }
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
        if (s) {
            switch (cmd->family) {
            case FMRB_LINK_GFX_FONT_FAMILY_DEFAULT:
                s->setFont(&fonts::Font0);
                break;
            case FMRB_LINK_GFX_FONT_FAMILY_JA:
                // size=8 -> misaki (matches the system 8px UI height),
                // 12 and 16 -> efontJA (bundled with M5GFX). Anything else
                // lands on 12: a caller that cares which it got asks
                // FmrbGfx#set_font, which answers from the same table.
                if (cmd->size == 8) {
                    s->setFont(&fonts::misaki_8);
                } else if (cmd->size == 16) {
                    s->setFont(&fonts::efontJA_16);
                } else {
                    if (cmd->size != 12) {
                        FMRB_LOGW(TAG, "SET_FONT: no JA %upx, using 12",
                                  cmd->size);
                    }
                    s->setFont(&fonts::efontJA_12);
                }
                break;
            case FMRB_LINK_GFX_FONT_FAMILY_JA_BOLD:
                // Only the 12px cut is carried. A heading is 16px and stands
                // out by its size and its band, so its bold is not worth the
                // flash.
                if (cmd->size != 12) {
                    FMRB_LOGW(TAG, "SET_FONT: no JA bold %upx, using 12",
                              cmd->size);
                }
                s->setFont(&fonts::efontJA_12_b);
                break;
            default:
                FMRB_LOGW(TAG, "SET_FONT: unknown family=%u", cmd->family);
                return -1;
            }
        }
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
        display_p4_video_canvas_gone(cmd->canvas_id);
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
            // Move the active area inside the already allocated buffer, the way
            // Retro does with setBuffer. Clamped because the buffer is only
            // framebuffer-sized: a larger request would draw out of bounds.
            uint16_t nw = (uint16_t)cmd->width;
            uint16_t nh = (uint16_t)cmd->height;
            if (nw > c->alloc_width)  nw = c->alloc_width;
            if (nh > c->alloc_height) nh = c->alloc_height;
            c->push_x = (int16_t)cmd->x;
            c->push_y = (int16_t)cmd->y;
            if ((nw != c->width || nh != c->height) && c->sprite) {
                void *buf = c->sprite->getBuffer();
                if (buf) c->sprite->setBuffer(buf, nw, nh);
                // Keep the committed buffer the same active size, so the commit
                // copy and the composite agree on dimensions.
                if (c->render_sprite) {
                    void *rbuf = c->render_sprite->getBuffer();
                    if (rbuf) c->render_sprite->setBuffer(rbuf, nw, nh);
                }
            }
            if (nw != c->width || nh != c->height) {
                // The sprite clip was sized for the old active area and may now
                // reach past the canvas. Drop it; the app resends one for the
                // new user area from its resize handler.
                c->clip_x = c->clip_y = c->clip_w = c->clip_h = 0;
            }
            c->width  = nw;
            c->height = nh;
            FMRB_LOGI(TAG, "UPDATE_WINDOW: id=%u %dx%d at (%d,%d)",
                      cmd->canvas_id, nw, nh, c->push_x, c->push_y);
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
            // This is "present" for one canvas: commit the working sprite into
            // the committed buffer the compositor reads. A raw copy of the
            // active area (stride == width, so it is contiguous) preserves the
            // color-key pixels the compositor needs. Only this canvas commits,
            // so an app still mid-drawing another canvas is unaffected.
            if (src->sprite && src->render_sprite) {
                void *sb = src->sprite->getBuffer();
                void *rb = src->render_sprite->getBuffer();
                if (sb && rb) {
                    memcpy(rb, sb, (size_t)src->width * src->height * 2);
                }
            }
            src->push_x           = (int16_t)cmd->x;
            src->push_y           = (int16_t)cmd->y;
            src->transparent_color = cmd->transparent_color;
            src->use_transparency  = (bool)cmd->use_transparency;
            src->is_visible        = true;
            g_needs_render = true;
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
        if (c && c->is_visible != (cmd->visible != 0)) {
            c->is_visible = (cmd->visible != 0);
            g_needs_render = true;
        }
        return 0;
    }

    case FMRB_LINK_GFX_PRESENT:
        g_needs_render = true;
        return 0;

    case FMRB_LINK_GFX_SET_CANVAS_VIEWPORT: {
        if (size < sizeof(fmrb_link_graphics_set_canvas_viewport_t)) break;
        const auto *cmd = (const fmrb_link_graphics_set_canvas_viewport_t *)data;
        p4_canvas_t *c = canvas_find(cmd->canvas_id);
        if (!c) {
            FMRB_LOGW(TAG, "SET_CANVAS_VIEWPORT: canvas %u not found", cmd->canvas_id);
            return 0;
        }
        if (cmd->view_w == 0 || cmd->view_h == 0) {
            c->view_x = c->view_y = c->view_w = c->view_h = 0;
        } else {
            // Torus addressing: the source origin wraps around the canvas,
            // so a ring-buffer canvas barely larger than the viewport can
            // scroll an arbitrarily large world. Only the view size is
            // clamped (it cannot exceed the canvas).
            c->view_x = cmd->src_x % c->width;
            c->view_y = cmd->src_y % c->height;
            c->view_w = (cmd->view_w < c->width)  ? cmd->view_w : c->width;
            c->view_h = (cmd->view_h < c->height) ? cmd->view_h : c->height;
        }
        g_needs_render = true;
        return 0;
    }

    case FMRB_LINK_GFX_SET_SPRITE_CLIP: {
        if (size < sizeof(fmrb_link_graphics_set_sprite_clip_t)) break;
        const auto *cmd = (const fmrb_link_graphics_set_sprite_clip_t *)data;
        p4_canvas_t *c = canvas_find(cmd->canvas_id);
        if (!c) {
            FMRB_LOGW(TAG, "SET_SPRITE_CLIP: canvas %u not found", cmd->canvas_id);
            return 0;
        }
        if (cmd->w == 0 || cmd->h == 0 || cmd->x >= c->width || cmd->y >= c->height) {
            c->clip_x = c->clip_y = c->clip_w = c->clip_h = 0;
        } else {
            c->clip_x = cmd->x;
            c->clip_y = cmd->y;
            uint16_t max_w = c->width - cmd->x;
            uint16_t max_h = c->height - cmd->y;
            c->clip_w = (cmd->w < max_w) ? cmd->w : max_w;
            c->clip_h = (cmd->h < max_h) ? cmd->h : max_h;
        }
        g_needs_render = true;
        return 0;
    }

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
        if (g_cursor_x != cmd->x || g_cursor_y != cmd->y) {
            g_cursor_x = cmd->x;
            g_cursor_y = cmd->y;
            // Small-patch update only; no full re-render for cursor moves.
            // Before the first full render the LCD content is undefined,
            // so just record the position (render_frame draws the patch).
            if (!g_first_render) cursor_overlay_update();
        }
        return 0;
    }

    case FMRB_LINK_GFX_CURSOR_SET_VISIBLE: {
        if (size < sizeof(fmrb_link_graphics_cursor_visible_t)) break;
        const auto *cmd = (const fmrb_link_graphics_cursor_visible_t *)data;
        if (g_cursor_visible != (bool)cmd->visible) {
            g_cursor_visible = cmd->visible;
            if (!g_first_render) cursor_overlay_update();
        }
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

    case FMRB_LINK_GFX_EXPORT_FRAME: {
        if (size < sizeof(fmrb_link_graphics_export_frame_t)) break;
        const auto *cmd = (const fmrb_link_graphics_export_frame_t *)data;
        if (size < sizeof(*cmd) + cmd->path_len) break;
        char full_path[256];
        resolve_vfs_path((const char *)(data + sizeof(*cmd)),
                         (int)cmd->path_len, full_path, sizeof(full_path));
        return export_frame_jpeg(full_path) ? 0 : -1;
    }

    case FMRB_LINK_GFX_LOAD_SPRITE_IMAGE_BMP: {
        if (size < sizeof(fmrb_link_graphics_load_sprite_image_bmp_t)) break;
        const auto *cmd = (const fmrb_link_graphics_load_sprite_image_bmp_t *)data;
        if (size < sizeof(*cmd) + cmd->path_len) break;

        // Paths are relative to the flash mount unless they name another
        // one. The player and the still loader share this rule, so an app can
        // say "/sd/movie/demo.mjpg" and "logo.png" in the same breath.
        const char *p  = (const char *)(data + sizeof(*cmd));
        int         pl = (int)cmd->path_len;
        char full_path[256];
        resolve_vfs_path(p, pl, full_path, sizeof(full_path));

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
        // Same event as the sprite module's own line just above; both are
        // debug-level for the same reason.
        FMRB_LOGD(TAG, "CREATE_SPRITE_INSTANCE: canvas=%u frames=%u -> id=%u",
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
        char full_path[256];
        resolve_vfs_path(p, pl, full_path, sizeof(full_path));

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

        // JPEG goes through the SoC decoder; PNG stays on LovyanGFX.
        if (display_p4_jpeg_is_jpeg(buf, (size_t)fsz)) {
            uint16_t jw = 0, jh = 0, jstride = 0;
            // The decoder needs its own input buffer alignment, so hand it a
            // copy rather than the plain heap block the file landed in.
            size_t in_alloc = 0;
            uint8_t *in = display_p4_jpeg_alloc_input((size_t)fsz, &in_alloc);
            uint8_t *pix = nullptr;
            if (in) {
                memcpy(in, buf, (size_t)fsz);
                pix = display_p4_jpeg_decode(in, (size_t)fsz, &jw, &jh, &jstride);
                display_p4_jpeg_free_input(in);
            }
            fmrb_sys_free(buf);
            if (!pix) {
                FMRB_LOGE(TAG, "CREATE_IMAGE_FROM_FILE: JPEG decode failed: %s",
                          full_path);
                send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
                return 1;
            }

            auto *jspr = new LGFX_Sprite();
            jspr->setColorDepth(lgfx::rgb565_nonswapped);
            jspr->setPsram(true);
            if (!jspr->createSprite(jw, jh)) {
                FMRB_LOGE(TAG, "CREATE_IMAGE_FROM_FILE: sprite alloc failed %ux%u",
                          jw, jh);
                display_p4_jpeg_free_output(pix);
                delete jspr;
                send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
                return 1;
            }
            uint16_t *dstpx = (uint16_t *)jspr->getBuffer();
            const uint16_t *srcpx = (const uint16_t *)pix;
            for (uint16_t y = 0; y < jh; y++) {
                memcpy(dstpx + (size_t)y * jw,
                       srcpx + (size_t)y * jstride,
                       (size_t)jw * 2);
            }
            display_p4_jpeg_free_output(pix);

            uint16_t jid = g_next_image_store_id++;
            if (jid == 0) jid = g_next_image_store_id++;
            slot->in_use   = true;
            slot->image_id = jid;
            slot->sprite   = jspr;
            slot->width    = jw;
            slot->height   = jh;

            resp.image_id = jid;
            resp.width    = jw;
            resp.height   = jh;
            FMRB_LOGI(TAG, "CREATE_IMAGE_FROM_FILE: %s -> id=%u %ux%u (JPEG)",
                      full_path, jid, jw, jh);
            send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
            return 1;
        }

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

    // --- Motion-JPEG playback ---
    // The player runs on its own task and only publishes decoded frames;
    // video_service() below copies them into the canvas from this task, so
    // canvas memory keeps a single writer.

    case FMRB_LINK_GFX_VIDEO_OPEN: {
        if (size < sizeof(fmrb_link_graphics_video_open_t)) break;
        const auto *cmd = (const fmrb_link_graphics_video_open_t *)data;
        if (size < sizeof(*cmd) + cmd->path_len) break;

        const char *p  = (const char *)(data + sizeof(*cmd));
        int         pl = (int)cmd->path_len;
        char full_path[256];
        resolve_vfs_path(p, pl, full_path, sizeof(full_path));

        fmrb_link_graphics_video_opened_t resp = {};
        uint16_t vw = 0, vh = 0;
        fmrb_err_t verr = FMRB_ERR_NOT_FOUND;
        if (!canvas_find(cmd->canvas_id)) {
            FMRB_LOGE(TAG, "VIDEO_OPEN: canvas %u not found", cmd->canvas_id);
        } else {
            verr = display_p4_video_open(cmd->canvas_id, cmd->x, cmd->y,
                                         full_path, cmd->fps, cmd->flags,
                                         &vw, &vh);
        }
        resp.result = (uint8_t)(verr == FMRB_OK ? 0 : (uint8_t)(-verr));
        resp.width  = vw;
        resp.height = vh;
        send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
        return 1;
    }

    case FMRB_LINK_GFX_VIDEO_CONTROL: {
        if (size < sizeof(fmrb_link_graphics_video_control_t)) break;
        const auto *cmd = (const fmrb_link_graphics_video_control_t *)data;
        display_p4_video_control(cmd->action);
        display_p4_video_status_t st = {};
        display_p4_video_get_status(&st);
        fmrb_link_graphics_video_status_t resp = {};
        resp.state          = st.state;
        resp.canvas_id      = st.canvas_id;
        resp.frames_shown   = st.frames_shown;
        resp.frames_dropped = st.frames_dropped;
        send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
        return 1;
    }

    case FMRB_LINK_GFX_VIDEO_STATUS: {
        display_p4_video_status_t st = {};
        display_p4_video_get_status(&st);
        fmrb_link_graphics_video_status_t resp = {};
        resp.state          = st.state;
        resp.canvas_id      = st.canvas_id;
        resp.frames_shown   = st.frames_shown;
        resp.frames_dropped = st.frames_dropped;
        send_ack(msg_type, seq, (const uint8_t *)&resp, sizeof(resp));
        return 1;
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
            // Scale is 8.8 fixed point, 0 meaning "as is" for x and "same as
            // x" for y (the graphics-audio side reads it the same way). At
            // 1:1 the plain push is cheaper; otherwise the sprite is pushed
            // through the zoom path with its pivot at the top-left corner, so
            // (x, y) stays the corner whatever the scale.
            float zx = cmd->scale_x_fp8 ? cmd->scale_x_fp8 / 256.0f : 1.0f;
            float zy = cmd->scale_y_fp8 ? cmd->scale_y_fp8 / 256.0f : zx;
            if (zx == 1.0f && zy == 1.0f) {
                img->sprite->pushSprite(dst, cmd->x, cmd->y);
            } else {
                img->sprite->setPivot(0.0f, 0.0f);
                img->sprite->pushRotateZoom(dst, (float)cmd->x, (float)cmd->y,
                                            0.0f, zx, zy);
            }
            FMRB_LOGI(TAG, "DRAW_IMAGE: id=%u -> canvas=%u (%d,%d) %ux%u x%.2f/%.2f",
                      cmd->image_id, cmd->canvas_id, cmd->x, cmd->y,
                      img->width, img->height, zx, zy);
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
        /* Dump the frame: "unpack failed" alone cannot distinguish a truncated
           frame from a well-formed one carrying garbage, and the two point at
           different layers (link framing vs. the sender's payload). */
        char hex[3 * 32 + 1];
        size_t shown = msgpack_len < 32 ? msgpack_len : 32;
        for (size_t i = 0; i < shown; i++) {
            snprintf(hex + i * 3, 4, "%02x ", msgpack_data[i]);
        }
        hex[shown ? shown * 3 - 1 : 0] = '\0';
        FMRB_LOGE(TAG, "msgpack unpack failed: ret=%d len=%u first%u=[%s]",
                  (int)ret, (unsigned)msgpack_len, (unsigned)shown, hex);
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
                    FMRB_LOGI(TAG, "Framebuffer allocated: %dx%d RGB565 PPA-native (scale=%s)",
                              g_display_width, g_display_height, DISPLAY_P4_SCALE_TEXT);
                }

                // Register the accelerators and take hold of the output
                // surface, now that the panel is up and the framebuffer exists.
                display_backend()->init(g_display_width, g_display_height);
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

    case FMRB_LINK_TYPE_GRAPHICS: {
        // An empty payload is a real command, not a malformed one: VIDEO_STATUS
        // asks a question and carries nothing. Skipping the handler for those
        // answered every such command with an empty ACK, which the caller reads
        // as "no data". Every case that dereferences the payload checks its
        // size first, so handing it a zero-length one is safe.
        int result = process_gfx_command(type, sub_cmd, seq, payload, payload_len);
        // 0 = success, no ACK yet; 1 = ACK already sent; -1 = error
        if (result == 0 && ack_required) {
            send_ack(type, seq, nullptr, 0);
        }
        break;
    }

    case FMRB_LINK_TYPE_AUDIO:
        // Packed audio command bytes (see audio_commands.h); on Modern
        // the local APU backend consumes them instead of the WROVER.
        if (payload && payload_len > 0) {
            audio_p4_process_command(payload, payload_len);
        }
        if (ack_required) send_ack(type, seq, nullptr, 0);
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
// Shared I2C service (lgfx path, mutex-serialized).
//
// The Tab5 internal bus (GPIO31/32) is driven by LovyanGFX at register
// level, so any i2c_master-driver access on this controller is
// corrupted once touch polling runs. Every runtime transaction must go
// through lgfx's I2C helpers under g_i2c_mutex. These wrappers are the
// only sanctioned entry points for other modules (audio volume,
// hw_proxy mediation for mruby apps).
// ============================================================

static bool i2c_service_lock(void) {
    if (!g_lcd_ready || !g_i2c_mutex) return false;
    return xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(500)) == pdTRUE;
}

static void i2c_service_unlock(void) {
    xSemaphoreGive(g_i2c_mutex);
}

#ifdef FMRB_PLATFORM_WASM
// No I2C bus exists here; the exported service answers "not supported" so any
// caller (audio volume, hw_proxy mediation) fails cleanly instead of linking
// against nothing.
extern "C" fmrb_err_t display_p4_i2c_write(uint8_t addr, const uint8_t *data,
                                           size_t len, uint32_t freq) {
    (void)addr; (void)data; (void)len; (void)freq;
    return FMRB_ERR_NOT_SUPPORTED;
}
extern "C" fmrb_err_t display_p4_i2c_read(uint8_t addr, uint8_t *data,
                                          size_t len, uint32_t freq) {
    (void)addr; (void)data; (void)len; (void)freq;
    return FMRB_ERR_NOT_SUPPORTED;
}
extern "C" fmrb_err_t display_p4_i2c_write_reg8(uint8_t addr, uint8_t reg,
                                                uint8_t value, uint32_t freq) {
    (void)addr; (void)reg; (void)value; (void)freq;
    return FMRB_ERR_NOT_SUPPORTED;
}
extern "C" void display_p4_poll_headphone(void) {}
#elif defined(FMRB_HW_NARYAV4)
// NARYA v4 has no touch controller, so LovyanGFX never drives this bus and the
// mixing hazard the Tab5 works around does not exist here: every transaction
// goes through the normal i2c_master driver on the bus the display created.
// The mutex stays, because the callers are still several tasks.
//
// A device handle per transaction rather than a cache: the addresses are not
// known in advance (an mruby app can talk to anything on the header), and at
// this traffic level -- volume changes, occasional app I/O -- the add/remove
// pair costs nothing worth keeping state for.
static fmrb_err_t naryav4_i2c_xfer(uint8_t addr, const uint8_t *tx, size_t tx_len,
                                   uint8_t *rx, size_t rx_len, uint32_t freq) {
    if (!g_naryav4_i2c) return FMRB_ERR_INVALID_STATE;
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = addr;
    dev_cfg.scl_speed_hz    = freq ? freq : NARYAV4_I2C_FREQ;
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(g_naryav4_i2c, &dev_cfg, &dev) != ESP_OK) {
        return FMRB_ERR_FAILED;
    }
    esp_err_t err;
    if (rx && rx_len) {
        err = i2c_master_transmit_receive(dev, tx, tx_len, rx, rx_len, 100);
    } else {
        err = i2c_master_transmit(dev, tx, tx_len, 100);
    }
    i2c_master_bus_rm_device(dev);
    return err == ESP_OK ? FMRB_OK : FMRB_ERR_FAILED;
}

extern "C" fmrb_err_t display_p4_i2c_write(uint8_t addr, const uint8_t *data,
                                           size_t len, uint32_t freq) {
    if (!data || len == 0 || len > 255) return FMRB_ERR_INVALID_PARAM;
    if (!i2c_service_lock()) return FMRB_ERR_INVALID_STATE;
    fmrb_err_t res = naryav4_i2c_xfer(addr, data, len, NULL, 0, freq);
    i2c_service_unlock();
    return res;
}

extern "C" fmrb_err_t display_p4_i2c_read(uint8_t addr, uint8_t *data,
                                          size_t len, uint32_t freq) {
    if (!data || len == 0 || len > 255) return FMRB_ERR_INVALID_PARAM;
    if (!i2c_service_lock()) return FMRB_ERR_INVALID_STATE;
    fmrb_err_t res = FMRB_ERR_INVALID_STATE;
    if (g_naryav4_i2c) {
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address  = addr;
        dev_cfg.scl_speed_hz    = freq ? freq : NARYAV4_I2C_FREQ;
        i2c_master_dev_handle_t dev = NULL;
        if (i2c_master_bus_add_device(g_naryav4_i2c, &dev_cfg, &dev) == ESP_OK) {
            res = i2c_master_receive(dev, data, len, 100) == ESP_OK
                ? FMRB_OK : FMRB_ERR_FAILED;
            i2c_master_bus_rm_device(dev);
        } else {
            res = FMRB_ERR_FAILED;
        }
    }
    i2c_service_unlock();
    return res;
}

extern "C" fmrb_err_t display_p4_i2c_write_reg8(uint8_t addr, uint8_t reg,
                                                uint8_t value, uint32_t freq) {
    if (!i2c_service_lock()) return FMRB_ERR_INVALID_STATE;
    const uint8_t buf[2] = { reg, value };
    fmrb_err_t res = naryav4_i2c_xfer(addr, buf, sizeof(buf), NULL, 0, freq);
    i2c_service_unlock();
    return res;
}

// No headphone jack detect on this board: the amplifier enable is a plain
// GPIO and there is nothing to sense (P3 wires it up).
extern "C" void display_p4_poll_headphone(void) {}
#else
extern "C" fmrb_err_t display_p4_i2c_write(uint8_t addr, const uint8_t *data,
                                           size_t len, uint32_t freq) {
    if (!data || len == 0 || len > 255) return FMRB_ERR_INVALID_PARAM;
    if (!i2c_service_lock()) return FMRB_ERR_INVALID_STATE;
    auto res = lgfx::i2c::transactionWrite(TAB5_I2C_PORT, addr, data,
                                           (uint8_t)len, freq);
    i2c_service_unlock();
    return res.has_error() ? FMRB_ERR_FAILED : FMRB_OK;
}

extern "C" fmrb_err_t display_p4_i2c_read(uint8_t addr, uint8_t *data,
                                          size_t len, uint32_t freq) {
    if (!data || len == 0 || len > 255) return FMRB_ERR_INVALID_PARAM;
    if (!i2c_service_lock()) return FMRB_ERR_INVALID_STATE;
    auto res = lgfx::i2c::transactionRead(TAB5_I2C_PORT, addr, data,
                                          (uint8_t)len, freq);
    i2c_service_unlock();
    return res.has_error() ? FMRB_ERR_FAILED : FMRB_OK;
}

extern "C" fmrb_err_t display_p4_i2c_write_reg8(uint8_t addr, uint8_t reg,
                                                uint8_t value, uint32_t freq) {
    if (!i2c_service_lock()) return FMRB_ERR_INVALID_STATE;
    auto res = lgfx::i2c::writeRegister8(TAB5_I2C_PORT, addr, reg, value,
                                         0, freq);
    i2c_service_unlock();
    return res.has_error() ? FMRB_ERR_FAILED : FMRB_OK;
}

// ============================================================
// Headphone jack detect / speaker amp gating (PI4IO #1).
//
// display_p4_poll_headphone() is called from the touch task loop; the
// PI4IO transactions take g_i2c_mutex like every other bus user.
// ============================================================

static bool g_headphone_in = false;
static int  g_hp_debounce = 0;

// Caller must hold g_i2c_mutex (or be in the single-task init window).
static void hp_apply_speaker(bool headphone_in) {
    // RMW of OUT_SET through the lgfx path; other bits (LCD/TP/CAM
    // resets, EXT5V) keep their state.
    if (headphone_in) {
        lgfx::i2c::bitOff(TAB5_I2C_PORT, PI4IO1_ADDR, PI4IO_REG_OUT_SET,
                          PI4IO_BIT_SPK_EN, PI4IO_I2C_FREQ);
    } else {
        lgfx::i2c::bitOn(TAB5_I2C_PORT, PI4IO1_ADDR, PI4IO_REG_OUT_SET,
                         PI4IO_BIT_SPK_EN, PI4IO_I2C_FREQ);
    }
    FMRB_LOGI(TAG, "Headphone %s: speaker %s",
              headphone_in ? "plugged" : "removed",
              headphone_in ? "muted" : "enabled");
}

extern "C" void display_p4_poll_headphone(void) {
    if (!i2c_service_lock()) return;

    auto res = lgfx::i2c::readRegister8(TAB5_I2C_PORT, PI4IO1_ADDR,
                                        PI4IO_REG_IN_STA, PI4IO_I2C_FREQ);
    if (res.has_error()) {
        i2c_service_unlock();
        return;
    }
    bool hp = (res.value() & PI4IO_BIT_HP_DETECT) != 0;

    if (hp == g_headphone_in) {
        g_hp_debounce = 0;
    } else if (++g_hp_debounce >= 2) {
        g_hp_debounce = 0;
        g_headphone_in = hp;
        hp_apply_speaker(hp);
    }
    i2c_service_unlock();
}
#endif /* !FMRB_PLATFORM_WASM */

// ============================================================
// Main task
// ============================================================

// ---- Boot screen (PC-98/DOS style, mirroring the WROVER GA boot screen) ----
// Plain white-on-black text drawn straight to the panel while the kernel is
// still booting. Cleared by the first render_frame (which also flushes the
// cached CPU writes, see g_first_render).

#define BOOT_TEXT_SIZE 3                    // 6x8 base font -> 18x24 on 720p
#define BOOT_CHAR_W    (6 * BOOT_TEXT_SIZE)
#define BOOT_CHAR_H    (8 * BOOT_TEXT_SIZE)
#define BOOT_LINE_H    (BOOT_CHAR_H + 4)
#define BOOT_MARGIN_X  8
#define BOOT_MARGIN_Y  8

static int s_boot_line_y = 0;
static int s_boot_cursor_x = 0;   // blinking block after the last status line
static int s_boot_cursor_y = 0;

#ifdef FMRB_PLATFORM_WASM
// No panel: the boot screen has nowhere to go, and the loading page is the
// browser's job anyway.
static void boot_print_line(const char *text) { (void)text; }
static void boot_print_blank(void) {}
static void draw_boot_cursor(bool visible) { (void)visible; }
static void draw_boot_screen(void) {}
#else
static void boot_print_line(const char *text) {
    g_lcd.setCursor(BOOT_MARGIN_X, s_boot_line_y);
    g_lcd.print(text);
    s_boot_cursor_x = BOOT_MARGIN_X + (int)strlen(text) * BOOT_CHAR_W + BOOT_CHAR_W / 2;
    s_boot_cursor_y = s_boot_line_y;
    s_boot_line_y += BOOT_LINE_H;
}

static void boot_print_blank(void) {
    s_boot_line_y += BOOT_LINE_H;
}

static void draw_boot_cursor(bool visible) {
    g_lcd.fillRect(s_boot_cursor_x, s_boot_cursor_y, BOOT_CHAR_W, BOOT_CHAR_H,
                   visible ? 0xFFFFFFu : 0x000000u);
}

static void draw_boot_screen(void) {
    char buf[80];

    g_lcd.fillScreen(0x000000u);
    g_lcd.setTextSize(BOOT_TEXT_SIZE);
    g_lcd.setTextColor(0xFFFFFFu, 0x000000u);
    s_boot_line_y = BOOT_MARGIN_Y;

    boot_print_line("Family mruby Modern System");
    boot_print_line("==========================");
    boot_print_blank();

    // Which build is on screen. The app description is written at link time,
    // so unlike __DATE__ / __TIME__ it cannot linger as a stale value through
    // an incremental build. Its version is git describe, so it carries the
    // commit and whether the tree was dirty.
    const esp_app_desc_t *app = esp_app_get_description();
    snprintf(buf, sizeof(buf), "Core: %.28s  link v%d", app->version, FMRB_LINK_VERSION);
    boot_print_line(buf);
    snprintf(buf, sizeof(buf), "Built: %s %s", app->date, app->time);
    boot_print_line(buf);
    // Whole idf_ver on a line of its own: cutting it at the first dash would
    // make a master build read as the release tag.
    snprintf(buf, sizeof(buf), "IDF: %.40s", app->idf_ver);
    boot_print_line(buf);
    boot_print_blank();

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    // chip.revision is encoded as major*100 + minor (IDF v5.x)
    snprintf(buf, sizeof(buf), "CPU: ESP32-P4 rev v%d.%d %d cores",
             chip.revision / 100, chip.revision % 100, chip.cores);
    boot_print_line(buf);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    snprintf(buf, sizeof(buf), "Flash: %lu MB",
             (unsigned long)(flash_size / (1024 * 1024)));
    boot_print_line(buf);

    snprintf(buf, sizeof(buf), "Free heap:  %lu bytes",
             (unsigned long)esp_get_free_heap_size());
    boot_print_line(buf);
    snprintf(buf, sizeof(buf), "Free PSRAM: %zu bytes",
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    boot_print_line(buf);
    boot_print_blank();

    snprintf(buf, sizeof(buf), "Display: %dx%d MIPI-DSI",
             (int)g_lcd.width(), (int)g_lcd.height());
    boot_print_line(buf);
}
#endif /* !FMRB_PLATFORM_WASM */

// Copy a frame the player finished into the canvas it owns, then commit it
// the same way a present would. Runs on the display task: the player never
// writes canvas memory itself, so the compositor's single-writer assumption
// holds. Returns true when a frame was taken.
static bool video_service(void) {
    const uint8_t *pixels = nullptr;
    uint16_t vw = 0, vh = 0, stride_px = 0, canvas_id = 0;
    int16_t dx = 0, dy = 0;
    if (!display_p4_video_take_frame(&pixels, &vw, &vh, &stride_px,
                                     &canvas_id, &dx, &dy)) {
        return false;
    }

    int64_t copy_t0 = (int64_t)fmrb_hal_time_get_us();
    p4_canvas_t *c = canvas_find(canvas_id);
    if (c && c->sprite && pixels) {
        uint16_t *dst = (uint16_t *)c->sprite->getBuffer();
        const uint16_t *src = (const uint16_t *)pixels;
        if (dst) {
            // Clip the picture to the canvas, top-left first so a negative
            // origin skips source rows/columns instead of writing behind the
            // buffer.
            int sx = 0, sy = 0;
            int px = dx, py = dy;
            if (px < 0) { sx = -px; px = 0; }
            if (py < 0) { sy = -py; py = 0; }
            int cols = (int)vw - sx;
            int rows = (int)vh - sy;
            if (cols > (int)c->width  - px) cols = (int)c->width  - px;
            if (rows > (int)c->height - py) rows = (int)c->height - py;
            if (cols > 0 && rows > 0) {
                for (int y = 0; y < rows; y++) {
                    memcpy(dst + (size_t)(py + y) * c->width + px,
                           src + (size_t)(sy + y) * stride_px + sx,
                           (size_t)cols * 2);
                }
                // Commit: the compositor only ever reads the committed buffer.
                // Visibility is left alone on purpose -- a parked window must
                // stay parked even while its player keeps running.
                if (c->render_sprite) {
                    void *rb = c->render_sprite->getBuffer();
                    if (rb) memcpy(rb, dst, (size_t)c->width * c->height * 2);
                }
                g_needs_render = true;
                display_p4_video_note_copy_us(
                    (uint32_t)((int64_t)fmrb_hal_time_get_us() - copy_t0));
            }
        }
    }

    display_p4_video_release_frame();
    return true;
}

static void display_p4_task(void *arg) {
    (void)arg;
#ifdef FMRB_PLATFORM_WASM
    // Sprite-only bring-up: no panel, no codec, no headphone jack. The
    // interpreter and compositor below run unchanged; presenting is the wasm
    // backend's RGBA conversion (wasm/backend/display_backend_wasm.cpp).
    FMRB_LOGI(TAG, "wasm display: VM + cursor init (sprite-only, no panel)");
    display_p4_vm_init();
    cursor_init();
    g_lcd_ready = true;
#else
#if defined(FMRB_HW_NARYAV4)
    FMRB_LOGI(TAG, "NARYAv4 display: I2C + DSI bus");
    bool configured = g_lcd.configure();
    g_naryav4_i2c = g_lcd.i2cBus();
#else
    FMRB_LOGI(TAG, "Tab5 display: power on");
    g_lcd.configure(tab5_power_on());
    const bool configured = true;
#endif

    FMRB_LOGI(TAG, "Modern display: VM + cursor init");
    display_p4_vm_init();
    cursor_init();

    FMRB_LOGI(TAG, "Modern display: LGFX init");
    if (!configured || !g_lcd.init()) {
        FMRB_LOGE(TAG, "LGFX init failed");
    } else {
#if !defined(FMRB_HW_NARYAV4)
        g_lcd.setRotation(3); // landscape: 1280x720 (native portrait 720x1280 rotated 90deg CCW)
#endif
        // NARYA v4 is landscape already (800x600 over HDMI); no rotation.
        FMRB_LOGI(TAG, "LGFX init OK (%dx%d)", g_lcd.width(), g_lcd.height());
        draw_boot_screen();

        // Bring up the audio codec now: on Tab5 the ES8388 shares the I2C
        // bus with GT911, and the touch task only starts polling after
        // g_lcd_ready below, so this is the race-free window.
        if (audio_backend()->init() != FMRB_OK) {
            FMRB_LOGW(TAG, "audio init failed (no sound)");
            boot_print_line("Audio: codec init FAILED (no sound)");
        } else {
            boot_print_line("Audio: codec ready");
        }
        boot_print_blank();
        boot_print_line("Waiting for kernel...");

#if !defined(FMRB_HW_NARYAV4)
        // Apply the initial headphone state so booting with headphones
        // plugged does not play the boot sound on the speaker. If lgfx's
        // I2C is not up yet the read fails silently and the touch-task
        // poll corrects the state within ~0.5 s.
        {
            auto hp_res = lgfx::i2c::readRegister8(TAB5_I2C_PORT, PI4IO1_ADDR,
                                                   PI4IO_REG_IN_STA,
                                                   PI4IO_I2C_FREQ);
            if (!hp_res.has_error() && (hp_res.value() & PI4IO_BIT_HP_DETECT)) {
                g_headphone_in = true;
                hp_apply_speaker(true);
            }
        }
#endif

        // Framebuffer is allocated later in INIT_DISPLAY when the kernel
        // reports the actual display_width x display_height.
        g_lcd_ready = true;

#ifdef PPA_VERIFICATION_TEST
        ppa_verification_test();
#endif
    }
#endif /* !FMRB_PLATFORM_WASM */

    FMRB_LOGI(TAG, "Modern display: entering command receive loop");

    // Render throughput stats live at file scope (g_stat_*); seed the window.
    g_stat_last_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    // Boot-screen cursor blink state
    uint32_t boot_cursor_ms = 0;
    bool boot_cursor_on = false;

    while (1) {
        // Blink the boot-screen cursor until the first real frame replaces
        // the boot screen. Same task as render_frame, so no draw race.
#ifndef FMRB_PLATFORM_WASM
        if (g_first_render && g_lcd_ready) {
            uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            if ((uint32_t)(now - boot_cursor_ms) >= 500) {
                boot_cursor_ms = now;
                boot_cursor_on = !boot_cursor_on;
                draw_boot_cursor(boot_cursor_on);
            }
        }
#endif

        // Hand any finished video frame to its canvas before deciding whether
        // to render, so a new frame joins this render instead of the next one.
        video_service();

        // Render when a present (or composition change) requested it, paced to
        // RENDER_MIN_INTERVAL_MS so bursts coalesce into one frame (~30fps cap).
        // Safe to run here regardless of what any app is doing: render_frame
        // reads only committed buffers, so it never composites a canvas mid-draw.
        if (g_needs_render) {
            uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            if ((uint32_t)(now - g_last_render_ms) >= RENDER_MIN_INTERVAL_MS) {
                g_needs_render = false;
                render_frame();
                g_last_render_ms = now;
                note_render(now);
            }
        }

        fmrb_link_message_t msg = {
            .data = g_recv_buf,
            .size = sizeof(g_recv_buf),
        };
        // Short receive timeout while a render is pending so the pacing deadline
        // is honored even if no further commands arrive. Video playback needs
        // the same short loop: its frames arrive without any command traffic.
        uint32_t timeout_ms = (g_needs_render || display_p4_video_is_active()) ? 5 : 100;
        fmrb_err_t err = fmrb_hal_link_local_receive_cmd(
            FMRB_LINK_CHANNEL_DEFAULT, &msg, timeout_ms);
        if (err == FMRB_OK && msg.size > 0) {
            process_message(g_recv_buf, msg.size);
        } else if (err != FMRB_OK && err != FMRB_ERR_TIMEOUT) {
            // Before the kernel brings the link up, receive fails without
            // blocking; yield instead of spinning. Harmless on the device,
            // load-bearing on the cooperative wasm port, where this spin
            // would starve the boot task that is about to create the link.
            fmrb_task_delay_ms(10);
        }
    }
}

extern "C" fmrb_err_t display_p4_task_init(void) {
    if (!g_i2c_mutex) {
        g_i2c_mutex = xSemaphoreCreateMutex();
        if (!g_i2c_mutex) {
            FMRB_LOGE(TAG, "Failed to create I2C mutex");
            return FMRB_ERR_FAILED;
        }
    }
    BaseType_t ok = xTaskCreatePinnedToCore(display_p4_task, "display_p4",
                                            FMRB_DISPLAY_P4_TASK_STACK_SIZE,
                                            NULL, FMRB_DISPLAY_P4_TASK_PRIORITY,
                                            NULL, FMRB_DISPLAY_P4_TASK_CORE);
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
#ifdef FMRB_PLATFORM_WASM
    (void)out_x; (void)out_y;
    return 0;
#else
    if (!i2c_service_lock()) return 0;
    // Ask for two points so the return value distinguishes one finger from
    // two (the touch task maps a two-finger tap to a right click). Only the
    // primary point's coordinates are reported.
    lgfx::touch_point_t tp[2];
    int count = g_lcd.getTouch(tp, 2);
    i2c_service_unlock();
    if (count > 0) {
        *out_x = tp[0].x;
        *out_y = tp[0].y;
    }
    return count;
#endif
}


// ============================================================
// Remote-desktop capture API (see the capture state block above).
// Enable/disable from any task; acquire/release from ONE reader task.
// ============================================================

// Reference-counted: the MJPEG and H.264 streamers enable independently
// and capture stays on until the last consumer disables it.
static int g_cap_refcount = 0;

extern "C" fmrb_err_t display_p4_capture_enable(bool enable) {
    if (enable) {
        if (g_cap_buf[0]) {
            g_cap_refcount++;
            g_cap_enabled = true;
            return FMRB_OK;
        }
        if (!g_framebuffer) return FMRB_ERR_INVALID_STATE;
        size_t need = (size_t)g_framebuffer->width()
                    * g_framebuffer->height() * 2;
        size_t aligned = 0;
        g_cap_buf[0] = (uint8_t *)ppa_alloc_buffer(need, &aligned);
        g_cap_buf[1] = (uint8_t *)ppa_alloc_buffer(need, &aligned);
        if (!g_cap_buf[0] || !g_cap_buf[1]) {
            if (g_cap_buf[0]) { heap_caps_free(g_cap_buf[0]); g_cap_buf[0] = NULL; }
            if (g_cap_buf[1]) { heap_caps_free(g_cap_buf[1]); g_cap_buf[1] = NULL; }
            return FMRB_ERR_NO_MEMORY;
        }
        g_cap_buf_size = need;
        if (!g_cap_sem) g_cap_sem = xSemaphoreCreateBinary();
        if (!g_cap_sem) return FMRB_ERR_NO_MEMORY;
        portENTER_CRITICAL(&g_cap_lock);
        g_cap_front = -1;
        g_cap_locked_idx = -1;
        g_cap_seq = 0;
        portEXIT_CRITICAL(&g_cap_lock);
        g_cap_refcount = 1;
        g_cap_enabled = true;
        FMRB_LOGI(TAG, "Capture enabled (%ux%u, 2x%u bytes)",
                  g_framebuffer->width(), g_framebuffer->height(),
                  (unsigned)need);
    } else {
        if (g_cap_refcount > 0) g_cap_refcount--;
        if (g_cap_refcount > 0) return FMRB_OK;  // another consumer active
        g_cap_enabled = false;
        // Do not free while a reader may hold a buffer; the reader must
        // release before disabling. Buffers are small enough to keep.
        portENTER_CRITICAL(&g_cap_lock);
        g_cap_front = -1;
        portEXIT_CRITICAL(&g_cap_lock);
        FMRB_LOGI(TAG, "Capture disabled");
    }
    return FMRB_OK;
}

extern "C" fmrb_err_t display_p4_capture_acquire(uint32_t min_seq,
                                                 uint32_t timeout_ms,
                                                 display_p4_capture_frame_t *out) {
    if (!out || !g_cap_enabled || !g_cap_sem) return FMRB_ERR_INVALID_STATE;

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        portENTER_CRITICAL(&g_cap_lock);
        bool ready = (g_cap_front >= 0 && g_cap_seq >= min_seq &&
                      g_cap_locked_idx < 0);
        if (ready) {
            g_cap_locked_idx = g_cap_front;
            out->pixels = (const uint16_t *)g_cap_buf[g_cap_locked_idx];
            out->seq = g_cap_seq;
        }
        portEXIT_CRITICAL(&g_cap_lock);
        if (ready) {
            out->width  = (uint16_t)g_framebuffer->width();
            out->height = (uint16_t)g_framebuffer->height();
            return FMRB_OK;
        }
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) return FMRB_ERR_TIMEOUT;
        xSemaphoreTake(g_cap_sem, deadline - now);
    }
}

extern "C" void display_p4_capture_release(void) {
    portENTER_CRITICAL(&g_cap_lock);
    g_cap_locked_idx = -1;
    portEXIT_CRITICAL(&g_cap_lock);
}

extern "C" void display_p4_capture_kick(void) {
    g_needs_render = true;
}

extern "C" void display_p4_get_cursor(int *x, int *y, bool *visible) {
    if (x) *x = g_cursor_x;
    if (y) *y = g_cursor_y;
    if (visible) *visible = g_cursor_visible;
}
