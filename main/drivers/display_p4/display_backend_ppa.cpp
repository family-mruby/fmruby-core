/*
 * display_backend_ppa.cpp - the Modern boards' hardware output path
 *
 * Moved out of display_p4_task.cpp (doc/wasm/, P3 T1). Compositing is PPA
 * Blend, and the finished frame (426x240, or 640x360 in the fullscreen
 * high-resolution mode) reaches the panel through PPA SRM in one pass,
 * writing straight into the DSI frame buffer. What that pass does
 * differs by board -- see the geometry block below -- but nothing here decides
 * what is drawn; see display_backend.h for where the line is.
 *
 * The cache work is the reason most of this cannot be shared: the framebuffer
 * and the canvases live in PSRAM and are written by the CPU, so every buffer
 * the PPA is about to read has to be flushed out of the cache first, and its
 * output invalidated before the CPU reads it back.
 */

#include "display_backend.h"

#include "fmrb_log.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_private/esp_cache_private.h"
#include "esp_heap_caps.h"
#include "fmrb_attr.h"

#include <cstring>

static const char *TAG = "display_ppa";

/* The panel's own frame buffer, and how the frame is laid onto it.
 *
 *   Tab5      720x1280 RGB565 portrait. The frame goes on rotated 90 degrees,
 *             filling the width and centred along the long side.
 *   NARYA v4  1280x720 RGB888 landscape over HDMI (the LT8912B bridge takes
 *             nothing but RGB888). No rotation: the monitor is already
 *             landscape.
 *
 * Both are 1280x720 seen in landscape. The scale is not fixed: it is the
 * largest whole number that fits the framebuffer handed to present() onto
 * that (display_scale_for) -- 3x for the 426x240 frame, which leaves a
 * one-pixel border at the ends of the long side, and 2x for the fullscreen
 * high-resolution frame (640x360), which fills the panel exactly.
 */
#if defined(FMRB_HW_NARYAV4)
#define DSI_FB_W 1280
#define DSI_FB_H 720
#define DSI_FB_BPP 3
#define LAND_W DSI_FB_W
#define LAND_H DSI_FB_H
#else
#define DSI_FB_W 720
#define DSI_FB_H 1280
#define DSI_FB_BPP 2
#define LAND_W DSI_FB_H
#define LAND_H DSI_FB_W
#endif

#define DSI_FB_BYTES ((size_t)DSI_FB_W * DSI_FB_H * DSI_FB_BPP)

static ppa_client_handle_t s_ppa_srm   = NULL;  /* scale (+ rotate) to the panel */
static ppa_client_handle_t s_ppa_blend = NULL;  /* canvas compositing, colour-keyed */
#if defined(FMRB_HW_NARYAV4)
static uint8_t            *s_dsi_fb    = nullptr;  /* RGB888: 3 bytes per pixel */
#else
static uint16_t           *s_dsi_fb    = nullptr;
#endif
static size_t              s_cache_line = 64;

/* ------------------------------------------------------------------ init */

static void ppa_init(int fb_w, int fb_h)
{
    esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &s_cache_line);

    ppa_client_config_t blend_cfg = {};
    blend_cfg.oper_type = PPA_OPERATION_BLEND;
    if (ppa_register_client(&blend_cfg, &s_ppa_blend) == ESP_OK) {
        FMRB_LOGI(TAG, "PPA Blend initialized for canvas compositing");
    } else {
        FMRB_LOGW(TAG, "PPA Blend init failed");
        s_ppa_blend = NULL;
    }

    ppa_client_config_t srm_cfg = {};
    srm_cfg.oper_type = PPA_OPERATION_SRM;
    if (ppa_register_client(&srm_cfg, &s_ppa_srm) == ESP_OK) {
        s_dsi_fb = (decltype(s_dsi_fb))display_p4_panel_framebuffer();
        if (s_dsi_fb) {
            FMRB_LOGI(TAG, "PPA SRM initialized: %dx%d -> DSI fb %dx%d @%p",
                      fb_w, fb_h, DSI_FB_W, DSI_FB_H, (void *)s_dsi_fb);
        } else {
            FMRB_LOGW(TAG, "DSI framebuffer unavailable, using software scaling");
            ppa_unregister_client(s_ppa_srm);
            s_ppa_srm = NULL;
        }
    } else {
        FMRB_LOGW(TAG, "PPA SRM init failed, using software scaling");
        s_ppa_srm = NULL;
    }
}

static void ppa_first_frame(void)
{
    display_p4_lcd()->fillScreen(0);
    if (s_dsi_fb) {
        /* Flush LovyanGFX's cached CPU writes (boot screen, fill) once; from
         * here on the DSI buffer is written by PPA DMA and the cursor patch
         * (which writes back its own region), so no dirty CPU cache lines may
         * remain to evict over DMA output. */
        esp_cache_msync(s_dsi_fb, DSI_FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }
}

/* The rectangle a fb_w x fb_h frame covers in the DSI buffer, native
 * coordinates. Shared by present, the border clear and the cursor patch, so
 * the three cannot disagree about where the picture is. */
static void dsi_image_rect(int fb_w, int fb_h, int *x, int *y, int *w, int *h)
{
    const int scale = display_scale_for(LAND_W, LAND_H, fb_w, fb_h);
#if defined(FMRB_HW_NARYAV4)
    *w = fb_w * scale;
    *h = fb_h * scale;
#else
    /* Rotated: the frame's height runs along the native width. */
    *w = fb_h * scale;
    *h = fb_w * scale;
#endif
    *x = (DSI_FB_W - *w) / 2;
    *y = (DSI_FB_H - *h) / 2;
}

/* -------------------------------------------------------- frame size
 *
 * The framebuffer handed to present() changes size when the fullscreen
 * high-resolution mode goes on or off, and the scale changes with it. The
 * border the new picture does not cover (one pixel along the long side at
 * 3x, none at 2x) must not keep what the previous mode drew there, so after
 * a present of a new size the border is painted black.
 *
 * The picture itself is written straight into the buffer being scanned, as it
 * always was. At a mode switch that shows for one frame as a tear between the
 * old and the new layout; doc/fullscreen_hires/report/h1.md (the section on
 * what was put on hold) records the attempts to remove it.
 *
 * In PSRAM: read once per present, and the internal RAM budget has no room
 * for new statics. */
FMRB_EXT_RAM_BSS_ATTR static int s_last_fb_w;
FMRB_EXT_RAM_BSS_ATTR static int s_last_fb_h;

/* Paint one rectangle of the DSI buffer black (native coordinates) and write
 * it back, so no dirty cache line is left to evict over PPA output later. */
static void dsi_clear_rect(int x, int y, int w, int h)
{
    if (!s_dsi_fb || w <= 0 || h <= 0) return;
    uint8_t *base = (uint8_t *)s_dsi_fb;
    for (int row = y; row < y + h; row++) {
        memset(base + ((size_t)row * DSI_FB_W + x) * DSI_FB_BPP, 0,
               (size_t)w * DSI_FB_BPP);
    }
    esp_cache_msync(base + (size_t)y * DSI_FB_W * DSI_FB_BPP,
                    (size_t)h * DSI_FB_W * DSI_FB_BPP,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

/* Everything of the DSI buffer outside (x, y, w, h), native coordinates. */
static void dsi_clear_outside(int x, int y, int w, int h)
{
    dsi_clear_rect(0, 0, DSI_FB_W, y);
    dsi_clear_rect(0, y + h, DSI_FB_W, DSI_FB_H - (y + h));
    dsi_clear_rect(0, y, x, h);
    dsi_clear_rect(x + w, y, DSI_FB_W - (x + w), h);
}

/* After a present: if the frame changed size since the last one, black out
 * what the new one leaves uncovered. */
static void note_presented_size(int fb_w, int fb_h, bool through_dsi)
{
    if (fb_w == s_last_fb_w && fb_h == s_last_fb_h) return;
    s_last_fb_w = fb_w;
    s_last_fb_h = fb_h;
    if (through_dsi) {
        int x, y, w, h;
        dsi_image_rect(fb_w, fb_h, &x, &y, &w, &h);
        dsi_clear_outside(x, y, w, h);
    } else {
        LGFX_Device *lcd = display_p4_lcd();
        const int scale = display_scale_for(lcd->width(), lcd->height(), fb_w, fb_h);
        const int w = fb_w * scale, h = fb_h * scale;
        display_lcd_clear_outside(lcd, (lcd->width() - w) / 2,
                                  (lcd->height() - h) / 2, w, h);
    }
}

static void ppa_shutdown(void)
{
    if (s_ppa_blend) { ppa_unregister_client(s_ppa_blend); s_ppa_blend = NULL; }
    if (s_ppa_srm)   { ppa_unregister_client(s_ppa_srm);   s_ppa_srm = NULL; }
    s_dsi_fb = nullptr;
}

/* ----------------------------------------------------------------- blend */

static void ppa_blend_block(const display_blend_req_t *r)
{
    if (!s_ppa_blend) return;

    void *fg_buf = (void *)r->fg;
    void *bg_buf = r->bg;

    /* Flush CPU cache to PSRAM so PPA DMA reads current pixel data. For the
     * canvas, flush only the rows the blend block reads (rounded to cache
     * lines): a scroll canvas may be much larger than the visible part. */
    esp_err_t sync_err;
    {
        uintptr_t row_start = (uintptr_t)fg_buf
            + (size_t)r->src_y * r->fg_pic_w * 2;
        size_t row_len = (size_t)r->h * r->fg_pic_w * 2;
        uintptr_t astart = row_start & ~(uintptr_t)(s_cache_line - 1);
        uintptr_t aend = (row_start + row_len + s_cache_line - 1)
            & ~(uintptr_t)(s_cache_line - 1);
        uintptr_t buf_end = (uintptr_t)fg_buf + r->fg_size;
        if (aend > buf_end) aend = buf_end;
        sync_err = esp_cache_msync((void *)astart, (size_t)(aend - astart),
                                   ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }
    if (sync_err != ESP_OK) FMRB_LOGE(TAG, "fg msync C2M failed: %d", sync_err);
    sync_err = esp_cache_msync(bg_buf, r->bg_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (sync_err != ESP_OK) FMRB_LOGE(TAG, "bg msync C2M failed: %d", sync_err);

    ppa_blend_oper_config_t blend = {};
    /* Background: framebuffer */
    blend.in_bg.buffer         = bg_buf;
    blend.in_bg.pic_w          = (uint32_t)r->bg_pic_w;
    blend.in_bg.pic_h          = (uint32_t)r->bg_pic_h;
    blend.in_bg.block_w        = (uint32_t)r->w;
    blend.in_bg.block_h        = (uint32_t)r->h;
    blend.in_bg.block_offset_x = (uint32_t)r->dst_x;
    blend.in_bg.block_offset_y = (uint32_t)r->dst_y;
    blend.in_bg.blend_cm       = PPA_BLEND_COLOR_MODE_RGB565;

    /* Foreground: canvas source block */
    blend.in_fg.buffer         = fg_buf;
    blend.in_fg.pic_w          = (uint32_t)r->fg_pic_w;
    blend.in_fg.pic_h          = (uint32_t)r->fg_pic_h;
    blend.in_fg.block_w        = (uint32_t)r->w;
    blend.in_fg.block_h        = (uint32_t)r->h;
    blend.in_fg.block_offset_x = (uint32_t)r->src_x;
    blend.in_fg.block_offset_y = (uint32_t)r->src_y;
    blend.in_fg.blend_cm       = PPA_BLEND_COLOR_MODE_RGB565;

    /* Output: framebuffer (in-place, Blend allows BG==OUT) */
    blend.out.buffer         = bg_buf;
    blend.out.buffer_size    = r->bg_size;
    blend.out.pic_w          = (uint32_t)r->bg_pic_w;
    blend.out.pic_h          = (uint32_t)r->bg_pic_h;
    blend.out.block_offset_x = (uint32_t)r->dst_x;
    blend.out.block_offset_y = (uint32_t)r->dst_y;
    blend.out.blend_cm       = PPA_BLEND_COLOR_MODE_RGB565;

    /* All sprites use PPA-native RGB565 (non-swapped); no byte swap needed */
    blend.fg_byte_swap = false;
    blend.bg_byte_swap = false;

    /* FG fully opaque */
    blend.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    blend.fg_alpha_fix_val     = 255;
    blend.bg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;

    if (r->color_key) {
        blend.fg_ck_en = true;
        blend.fg_ck_rgb_low_thres  = {.b = r->ck_b_low,
                                      .g = r->ck_g_low,
                                      .r = r->ck_r_low};
        blend.fg_ck_rgb_high_thres = {.b = r->ck_b_high,
                                      .g = r->ck_g_high,
                                      .r = r->ck_r_high};
    }

    blend.mode = PPA_TRANS_MODE_BLOCKING;

    esp_err_t err = ppa_do_blend(s_ppa_blend, &blend);
    if (err == ESP_OK) {
        /* Invalidate output cache so CPU sees DMA-written data */
        esp_cache_msync(bg_buf, r->bg_size,
                        ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
    } else {
        FMRB_LOGE(TAG, "PPA Blend failed: %d (canvas=%u viewport=%d)",
                  err, r->canvas_id, (int)r->is_viewport);
        if (r->is_viewport) {
            /* Fallback for viewport canvases in case the PPA rejects the source
             * block: opaque CPU row copy. The CPU writes stay in cache and are
             * flushed by the framebuffer C2M msync before SRM. */
            const uint16_t *src = (const uint16_t *)fg_buf
                + (size_t)r->src_y * r->fg_pic_w + r->src_x;
            uint16_t *dst = (uint16_t *)bg_buf
                + (size_t)r->dst_y * r->bg_pic_w + r->dst_x;
            for (int row = 0; row < r->h; row++) {
                memcpy(dst + (size_t)row * r->bg_pic_w,
                       src + (size_t)row * r->fg_pic_w, (size_t)r->w * 2);
            }
        }
    }
}

/* --------------------------------------------------------------- present */

/* Common to both boards: hand the framebuffer's cached CPU writes to PSRAM so
 * the SRM DMA reads what was just drawn. */
static void present_flush(void *fb_ptr, size_t fb_size)
{
    esp_err_t sync_err = esp_cache_msync(fb_ptr, fb_size,
                                         ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (sync_err != ESP_OK) FMRB_LOGE(TAG, "fb msync C2M failed: %d", sync_err);
}

/* The fallback when there is no SRM client or no reachable panel buffer: push
 * through LovyanGFX instead, scaled and centred the same way. */
static void present_software(LGFX_Sprite *fb, int fb_w, int fb_h)
{
    LGFX_Device *lcd = display_p4_lcd();
    const float scale =
        (float)display_scale_for(lcd->width(), lcd->height(), fb_w, fb_h);
    int scaled_w = (int)(fb_w * scale);
    int scaled_h = (int)(fb_h * scale);
    int center_x = (lcd->width()  - scaled_w) / 2 + scaled_w / 2;
    int center_y = (lcd->height() - scaled_h) / 2 + scaled_h / 2;
    fb->pushRotateZoom(lcd, (float)center_x, (float)center_y, 0.0f,
                       scale, scale);
    note_presented_size(fb_w, fb_h, false);
}

#if defined(FMRB_HW_NARYAV4)

/* NARYA v4: one SRM pass does the scale AND the RGB565 -> RGB888 conversion
 * the HDMI bridge needs -- the SRM's input and output colour modes are
 * independent, so this costs no more than the Tab5's scale-and-rotate. The
 * result lands in the middle of the 1280x720 buffer; the border around it is
 * black (first_frame, and again whenever the frame changes size). */
static void ppa_present(LGFX_Sprite *fb, size_t fb_size)
{
    int fb_w = fb->width();
    int fb_h = fb->height();
    void *fb_ptr = fb->getBuffer();

    if (!s_ppa_srm || !s_dsi_fb) {
        present_software(fb, fb_w, fb_h);
        return;
    }

    present_flush(fb_ptr, fb_size);

    const float scale = (float)display_scale_for(LAND_W, LAND_H, fb_w, fb_h);
    int out_x, out_y, out_w, out_h;
    dsi_image_rect(fb_w, fb_h, &out_x, &out_y, &out_w, &out_h);

    ppa_srm_oper_config_t srm = {};
    srm.in.buffer         = fb_ptr;
    srm.in.pic_w          = (uint32_t)fb_w;
    srm.in.pic_h          = (uint32_t)fb_h;
    srm.in.block_w        = (uint32_t)fb_w;
    srm.in.block_h        = (uint32_t)fb_h;
    srm.in.block_offset_x = 0;
    srm.in.block_offset_y = 0;
    srm.in.srm_cm         = PPA_SRM_COLOR_MODE_RGB565;

    srm.out.buffer         = s_dsi_fb;
    srm.out.buffer_size    = (uint32_t)DSI_FB_BYTES;
    srm.out.pic_w          = DSI_FB_W;
    srm.out.pic_h          = DSI_FB_H;
    srm.out.block_offset_x = (uint32_t)out_x;
    srm.out.block_offset_y = (uint32_t)out_y;
    srm.out.srm_cm         = PPA_SRM_COLOR_MODE_RGB888;

    srm.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    srm.scale_x        = scale;
    srm.scale_y        = scale;
    srm.mirror_x       = false;
    srm.mirror_y       = false;
    srm.rgb_swap       = false;
    srm.byte_swap      = false;
    srm.mode           = PPA_TRANS_MODE_BLOCKING;

    esp_err_t err = ppa_do_scale_rotate_mirror(s_ppa_srm, &srm);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "PPA SRM failed: %d", err);
    }
    note_presented_size(fb_w, fb_h, true);
}

/* Cursor fast path. The mapping has to be the one the SRM applies, or the
 * patch does not line up with the frame under it: every input pixel becomes
 * a scale x scale block (nearest, no filtering), and the picture starts at the
 * border offset. RGB888 is stored B,G,R -- that is what "non-swapped" means in
 * both LovyanGFX and the PPA, and it is the order the DPI panel scans out. */
static void ppa_present_patch(const uint16_t *block, int x0, int y0, int w, int h,
                              int fb_w, int fb_h)
{
    if (!s_dsi_fb) {
        /* Without the panel buffer there is no cheap path; the next full
         * present puts the cursor on screen. */
        return;
    }

    const int scale = display_scale_for(LAND_W, LAND_H, fb_w, fb_h);
    int off_x, off_y, out_w, out_h;
    dsi_image_rect(fb_w, fb_h, &off_x, &off_y, &out_w, &out_h);

    const int ox_begin = x0 * scale;
    const int ox_end   = (x0 + w) * scale;
    const int oy_begin = y0 * scale;
    const int oy_end   = (y0 + h) * scale;

    for (int oy = oy_begin; oy < oy_end; oy++) {
        int sy = oy / scale;
        const uint16_t *row = block + (size_t)(sy - y0) * w;
        uint8_t *dst_row = s_dsi_fb
            + ((size_t)(off_y + oy) * DSI_FB_W + off_x) * DSI_FB_BPP;
        for (int ox = ox_begin; ox < ox_end; ox++) {
            int sx = ox / scale;
            uint16_t px = row[sx - x0];
            uint8_t *d = dst_row + (size_t)ox * DSI_FB_BPP;
            uint8_t r5 = (uint8_t)((px >> 11) & 0x1F);
            uint8_t g6 = (uint8_t)((px >> 5)  & 0x3F);
            uint8_t b5 = (uint8_t)( px        & 0x1F);
            d[0] = (uint8_t)((b5 << 3) | (b5 >> 2));
            d[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
            d[2] = (uint8_t)((r5 << 3) | (r5 >> 2));
        }
    }

    /* Write the touched rows back, so a dirty cache line cannot evict later
     * over PPA-written frame data. */
    uint8_t *span = s_dsi_fb
        + (size_t)(off_y + oy_begin) * DSI_FB_W * DSI_FB_BPP;
    size_t span_len = (size_t)(oy_end - oy_begin) * DSI_FB_W * DSI_FB_BPP;
    esp_cache_msync(span, span_len,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

#else /* Tab5 */

static void ppa_present(LGFX_Sprite *fb, size_t fb_size)
{
    int fb_w = fb->width();
    int fb_h = fb->height();
    void *fb_ptr = fb->getBuffer();

    if (!s_ppa_srm || !s_dsi_fb) {
        /* Software scaling, kept as the same fallback it has always been. */
        present_software(fb, fb_w, fb_h);
        return;
    }

    present_flush(fb_ptr, fb_size);

    /* Scale and rotate to native portrait in one hardware pass, writing
     * directly into the DSI framebuffer (no CPU copy, and no M2C invalidate:
     * the CPU never reads the SRM output). The rotated output is fb_h*scale
     * (=720) wide and fb_w*scale high (1278 at 3x, 1280 at 2x), centred
     * vertically in the 1280-high native framebuffer. */
    const float scale = (float)display_scale_for(LAND_W, LAND_H, fb_w, fb_h);
    int out_x, out_y, out_w, out_h;
    dsi_image_rect(fb_w, fb_h, &out_x, &out_y, &out_w, &out_h);

    ppa_srm_oper_config_t srm = {};
    srm.in.buffer         = fb_ptr;
    srm.in.pic_w          = (uint32_t)fb_w;
    srm.in.pic_h          = (uint32_t)fb_h;
    srm.in.block_w        = (uint32_t)fb_w;
    srm.in.block_h        = (uint32_t)fb_h;
    srm.in.block_offset_x = 0;
    srm.in.block_offset_y = 0;
    srm.in.srm_cm         = PPA_SRM_COLOR_MODE_RGB565;

    srm.out.buffer         = s_dsi_fb;
    srm.out.buffer_size    = (uint32_t)DSI_FB_BYTES;
    srm.out.pic_w          = DSI_FB_W;
    srm.out.pic_h          = DSI_FB_H;
    srm.out.block_offset_x = (uint32_t)out_x;
    srm.out.block_offset_y = (uint32_t)out_y;
    srm.out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565;

    /* Logical landscape -> native portrait (confirmed on device) */
    srm.rotation_angle = PPA_SRM_ROTATION_ANGLE_90;
    srm.scale_x        = scale;
    srm.scale_y        = scale;
    srm.mirror_x       = false;
    srm.mirror_y       = false;
    srm.rgb_swap       = false;
    srm.byte_swap      = false;  /* All buffers use PPA-native RGB565 */
    srm.mode           = PPA_TRANS_MODE_BLOCKING;

    esp_err_t err = ppa_do_scale_rotate_mirror(s_ppa_srm, &srm);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "PPA SRM failed: %d", err);
    }
    note_presented_size(fb_w, fb_h, true);
}

/* Cursor fast path: the same logical->native mapping the SRM rotation applies
 * (ANGLE_90): nx = off_x + iy, ny = off_y + imgH - 1 - ix. Writing the patch
 * straight into the DSI buffer is what makes a cursor move cost a few KB
 * instead of a full frame, and it has to match the rotation exactly or the
 * cursor lands elsewhere than the frame it sits on. */
static void ppa_present_patch(const uint16_t *block, int x0, int y0, int w, int h,
                              int fb_w, int fb_h)
{
    const int scale = display_scale_for(LAND_W, LAND_H, fb_w, fb_h);

    if (!s_dsi_fb) {
        /* No direct access to the panel's buffer: scale into a temporary and
         * push it through LovyanGFX instead. */
        static uint16_t scaled[DISPLAY_PATCH_MAX_W * DISPLAY_MAX_SCALE *
                               DISPLAY_PATCH_MAX_H * DISPLAY_MAX_SCALE];
        if (w > DISPLAY_PATCH_MAX_W || h > DISPLAY_PATCH_MAX_H) return;
        if (scale > DISPLAY_MAX_SCALE) return;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint16_t px = block[(size_t)y * w + x];
                for (int dy = 0; dy < scale; dy++) {
                    uint16_t *o = scaled + (size_t)(y * scale + dy) * (w * scale)
                                + x * scale;
                    for (int dx = 0; dx < scale; dx++) o[dx] = px;
                }
            }
        }
        LGFX_Device *lcd = display_p4_lcd();
        int offset_x = (lcd->width()  - fb_w * scale) / 2;
        int offset_y = (lcd->height() - fb_h * scale) / 2;
        lcd->pushImage(offset_x + x0 * scale, offset_y + y0 * scale,
                       w * scale, h * scale, (lgfx::rgb565_t *)scaled);
        return;
    }

    int off_x, off_y, img_w_native, img_h_native;
    dsi_image_rect(fb_w, fb_h, &off_x, &off_y, &img_w_native, &img_h_native);
    for (int y = 0; y < h; y++) {
        const uint16_t *row = block + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            uint16_t px = row[x];
            for (int dy = 0; dy < scale; dy++) {
                int nx = off_x + (y0 + y) * scale + dy;
                for (int dx = 0; dx < scale; dx++) {
                    int ix = (x0 + x) * scale + dx;
                    int ny = off_y + img_h_native - 1 - ix;
                    s_dsi_fb[(size_t)ny * DSI_FB_W + nx] = px;
                }
            }
        }
    }

    /* Write back the affected native rows so dirty cache lines cannot evict
     * later over PPA-written frame data. Rows ny span
     * [off_y + imgH - (x0+w)*S, off_y + imgH - x0*S). C2M writeback tolerates
     * unaligned spans with the UNALIGNED flag. */
    int span_row = off_y + img_h_native - (x0 + w) * scale;
    uint8_t *span = (uint8_t *)&s_dsi_fb[(size_t)span_row * DSI_FB_W];
    size_t span_len = (size_t)(w * scale) * DSI_FB_W * 2;
    esp_cache_msync(span, span_len,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

#endif /* FMRB_HW_NARYAV4 */

/* ------------------------------------------------------------------------ */

static const display_backend_t s_backend_ppa = {
    .name        = "ppa",
    .init        = ppa_init,
    .first_frame = ppa_first_frame,
    .blend_block = ppa_blend_block,
    .present       = ppa_present,
    .present_patch = ppa_present_patch,
    .shutdown      = ppa_shutdown,
};

const display_backend_t *display_backend_ppa(void)
{
    return &s_backend_ppa;
}
