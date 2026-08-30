/*
 * display_backend_ppa.cpp - the Modern boards' hardware output path
 *
 * Moved out of display_p4_task.cpp (doc/wasm/, P3 T1). Compositing is PPA
 * Blend, and the finished 426x240 frame reaches the panel through PPA SRM in
 * one pass, writing straight into the DSI frame buffer. What that pass does
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

#include <cstring>

static const char *TAG = "display_ppa";

/* The panel's own frame buffer, and how the 426x240 frame is laid onto it.
 *
 *   Tab5      720x1280 RGB565 portrait. The frame goes on 3x and rotated 90
 *             degrees, filling the width and centred along the long side.
 *   NARYA v4  800x600 RGB888 landscape over HDMI (the LT8912B bridge takes
 *             nothing but RGB888). 426x240 does not scale to 800x600 by a
 *             whole number, so the frame goes on at 1.5x = 639x360 -- the PPA
 *             scaler works in 1/16 steps, so 1.5 is exact -- centred inside a
 *             black border of 80/81 px left/right and 120 px top and bottom.
 *             No rotation: the monitor is already landscape.
 */
#if defined(FMRB_HW_NARYAV4)
#define DSI_FB_W 800
#define DSI_FB_H 600
#define DSI_FB_BPP 3
/* 1.5x as an exact ratio, for the integer arithmetic in the cursor patch. */
#define SCALE_NUM 3
#define SCALE_DEN 2
#define SCALE_FLOAT 1.5f
#else
#define DSI_FB_W 720
#define DSI_FB_H 1280
#define DSI_FB_BPP 2
#define SCALE_FACTOR 3
#define SCALE_FLOAT ((float)SCALE_FACTOR)
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
    int scaled_w = (int)(fb_w * SCALE_FLOAT);
    int scaled_h = (int)(fb_h * SCALE_FLOAT);
    LGFX_Device *lcd = display_p4_lcd();
    int center_x = (lcd->width()  - scaled_w) / 2 + scaled_w / 2;
    int center_y = (lcd->height() - scaled_h) / 2 + scaled_h / 2;
    fb->pushRotateZoom(lcd, (float)center_x, (float)center_y, 0.0f,
                       SCALE_FLOAT, SCALE_FLOAT);
}

#if defined(FMRB_HW_NARYAV4)

/* NARYA v4: one SRM pass does the 1.5x scale AND the RGB565 -> RGB888
 * conversion the HDMI bridge needs -- the SRM's input and output colour modes
 * are independent, so this costs no more than the Tab5's scale-and-rotate. The
 * result lands in the middle of the 800x600 buffer; the border around it is
 * painted black once (first_frame) and never touched again. */
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

    const int out_w = fb_w * SCALE_NUM / SCALE_DEN;
    const int out_h = fb_h * SCALE_NUM / SCALE_DEN;

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
    srm.out.block_offset_x = (uint32_t)((DSI_FB_W - out_w) / 2);
    srm.out.block_offset_y = (uint32_t)((DSI_FB_H - out_h) / 2);
    srm.out.srm_cm         = PPA_SRM_COLOR_MODE_RGB888;

    srm.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    srm.scale_x        = SCALE_FLOAT;
    srm.scale_y        = SCALE_FLOAT;
    srm.mirror_x       = false;
    srm.mirror_y       = false;
    srm.rgb_swap       = false;
    srm.byte_swap      = false;
    srm.mode           = PPA_TRANS_MODE_BLOCKING;

    esp_err_t err = ppa_do_scale_rotate_mirror(s_ppa_srm, &srm);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "PPA SRM failed: %d", err);
    }
}

/* Cursor fast path. The mapping has to be the one the SRM applies, or the
 * patch does not line up with the frame under it: output column ox comes from
 * input column ox*2/3 (nearest, no filtering), and the picture starts at the
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

    const int off_x = (DSI_FB_W - fb_w * SCALE_NUM / SCALE_DEN) / 2;
    const int off_y = (DSI_FB_H - fb_h * SCALE_NUM / SCALE_DEN) / 2;

    const int ox_begin = x0 * SCALE_NUM / SCALE_DEN;
    const int ox_end   = (x0 + w) * SCALE_NUM / SCALE_DEN;
    const int oy_begin = y0 * SCALE_NUM / SCALE_DEN;
    const int oy_end   = (y0 + h) * SCALE_NUM / SCALE_DEN;

    for (int oy = oy_begin; oy < oy_end; oy++) {
        int sy = oy * SCALE_DEN / SCALE_NUM;
        if (sy < y0 || sy >= y0 + h) continue;
        const uint16_t *row = block + (size_t)(sy - y0) * w;
        uint8_t *dst_row = s_dsi_fb
            + ((size_t)(off_y + oy) * DSI_FB_W + off_x) * DSI_FB_BPP;
        for (int ox = ox_begin; ox < ox_end; ox++) {
            int sx = ox * SCALE_DEN / SCALE_NUM;
            if (sx < x0 || sx >= x0 + w) continue;
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

    /* Scale 3x and rotate to native portrait in one hardware pass, writing
     * directly into the DSI framebuffer (no CPU copy, and no M2C invalidate:
     * the CPU never reads the SRM output). Rotated output is fb_h*3 (=720)
     * wide, fb_w*3 (=1278) high; center it vertically in the 1280-high native
     * framebuffer. */
    int out_native_h = fb_w * SCALE_FACTOR;

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
    srm.out.block_offset_x = 0;
    srm.out.block_offset_y = (uint32_t)((DSI_FB_H - out_native_h) / 2);
    srm.out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565;

    /* Logical landscape -> native portrait (confirmed on device) */
    srm.rotation_angle = PPA_SRM_ROTATION_ANGLE_90;
    srm.scale_x        = (float)SCALE_FACTOR;
    srm.scale_y        = (float)SCALE_FACTOR;
    srm.mirror_x       = false;
    srm.mirror_y       = false;
    srm.rgb_swap       = false;
    srm.byte_swap      = false;  /* All buffers use PPA-native RGB565 */
    srm.mode           = PPA_TRANS_MODE_BLOCKING;

    esp_err_t err = ppa_do_scale_rotate_mirror(s_ppa_srm, &srm);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "PPA SRM failed: %d", err);
    }
}

/* Cursor fast path: the same logical->native mapping the SRM rotation applies
 * (ANGLE_90): nx = iy, ny = margin + imgH - 1 - ix. Writing the patch straight
 * into the DSI buffer is what makes a cursor move cost 4.6 KB instead of a full
 * frame, and it has to match the rotation exactly or the cursor lands elsewhere
 * than the frame it sits on. */
static void ppa_present_patch(const uint16_t *block, int x0, int y0, int w, int h,
                              int fb_w, int fb_h)
{
    (void)fb_h;

    if (!s_dsi_fb) {
        /* No direct access to the panel's buffer: scale into a temporary and
         * push it through LovyanGFX instead. */
        static uint16_t scaled[DISPLAY_PATCH_MAX_W * SCALE_FACTOR *
                               DISPLAY_PATCH_MAX_H * SCALE_FACTOR];
        if (w > DISPLAY_PATCH_MAX_W || h > DISPLAY_PATCH_MAX_H) return;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint16_t px = block[(size_t)y * w + x];
                for (int dy = 0; dy < SCALE_FACTOR; dy++) {
                    uint16_t *o = scaled + (size_t)(y * SCALE_FACTOR + dy) * (w * SCALE_FACTOR)
                                + x * SCALE_FACTOR;
                    for (int dx = 0; dx < SCALE_FACTOR; dx++) o[dx] = px;
                }
            }
        }
        LGFX_Device *lcd = display_p4_lcd();
        int offset_x = (lcd->width() - fb_w * SCALE_FACTOR) / 2;
        lcd->pushImage(offset_x + x0 * SCALE_FACTOR, y0 * SCALE_FACTOR,
                       w * SCALE_FACTOR, h * SCALE_FACTOR,
                       (lgfx::rgb565_t *)scaled);
        return;
    }

    const int img_h_native = fb_w * SCALE_FACTOR;
    const int margin = ((int)DSI_FB_H - img_h_native) / 2;
    for (int y = 0; y < h; y++) {
        const uint16_t *row = block + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            uint16_t px = row[x];
            for (int dy = 0; dy < SCALE_FACTOR; dy++) {
                int nx = (y0 + y) * SCALE_FACTOR + dy;
                for (int dx = 0; dx < SCALE_FACTOR; dx++) {
                    int ix = (x0 + x) * SCALE_FACTOR + dx;
                    int ny = margin + img_h_native - 1 - ix;
                    s_dsi_fb[(size_t)ny * DSI_FB_W + nx] = px;
                }
            }
        }
    }

    /* Write back the affected native rows so dirty cache lines cannot evict
     * later over PPA-written frame data. Rows ny span
     * [margin + imgH - (x0+w)*S, margin + imgH - x0*S). C2M writeback tolerates
     * unaligned spans with the UNALIGNED flag. */
    int span_row = margin + img_h_native - (x0 + w) * SCALE_FACTOR;
    uint8_t *span = (uint8_t *)&s_dsi_fb[(size_t)span_row * DSI_FB_W];
    size_t span_len = (size_t)(w * SCALE_FACTOR) * DSI_FB_W * 2;
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
