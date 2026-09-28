// P4 hardware JPEG encoder wrapper (MJPEG stream, Phase 1).
//
// The JPEG peripheral takes RGB565 directly (internal RGB->YUV420), so no
// color conversion is needed. MCU alignment requires the width to be a
// multiple of 16: the 426px framebuffer rows are copied into a 432px
// staging buffer with black padding on the right (the viewer crops it).
// The frame size is not fixed: rd_encoder_jpeg_init() called again with
// another size (the fullscreen high-resolution 640x360 frame) re-sizes the
// buffers, and the caller does that before each encode.
//
// NOTE (device verification): the framebuffer is RGB565 non-swapped
// little-endian (PPA-native). If red/blue appear swapped in the browser,
// switch the RGB element order via jpeg_encode_cfg_t.

#include "rd_encoder_jpeg.h"

#include "fmrb_log.h"
#include "driver/jpeg_encode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "rd_jpeg";

static jpeg_encoder_handle_t s_encoder = NULL;
static uint8_t *s_stage = NULL;        // padded RGB565 input (DMA-capable)
static size_t   s_stage_size = 0;
static uint8_t *s_out = NULL;          // JPEG output (DMA-capable)
static size_t   s_out_size = 0;
static uint16_t s_src_w = 0, s_src_h = 0;
static uint16_t s_pad_w = 0;
static uint8_t  s_quality = 80;
// Guards the two shared buffers for the whole of an encode-and-use; see the
// header. Created on the first lock so a caller that arrives before init
// still serialises.
static SemaphoreHandle_t s_mutex = NULL;
static portMUX_TYPE s_mutex_lock = portMUX_INITIALIZER_UNLOCKED;

fmrb_err_t rd_encoder_jpeg_lock(uint32_t timeout_ms)
{
    if (!s_mutex) {
        SemaphoreHandle_t m = xSemaphoreCreateMutex();
        if (!m) return FMRB_ERR_NO_MEMORY;
        portENTER_CRITICAL(&s_mutex_lock);
        if (s_mutex) {
            portEXIT_CRITICAL(&s_mutex_lock);
            vSemaphoreDelete(m);
        } else {
            s_mutex = m;
            portEXIT_CRITICAL(&s_mutex_lock);
        }
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return FMRB_ERR_TIMEOUT;
    }
    return FMRB_OK;
}

void rd_encoder_jpeg_unlock(void)
{
    if (s_mutex) xSemaphoreGive(s_mutex);
}

// Make the staging and output buffers fit a src_w x src_h frame. They only
// ever grow: going back to a smaller frame (leaving the fullscreen
// high-resolution mode) keeps the larger pair and uses the front of it, so
// switching modes back and forth does not churn PSRAM.
static fmrb_err_t ensure_buffers(uint16_t src_w, uint16_t src_h)
{
    const uint16_t pad_w = (uint16_t)((src_w + 15u) & ~15u);
    const size_t stage_need = (size_t)pad_w * src_h * 2;
    // Worst-case JPEG for this content class is far below raw size; half
    // of the raw frame is a comfortable ceiling at quality <= 90.
    const size_t out_need = (size_t)pad_w * src_h;

    if (stage_need > s_stage_size || out_need > s_out_size) {
        if (s_stage) { free(s_stage); s_stage = NULL; s_stage_size = 0; }
        if (s_out)   { free(s_out);   s_out = NULL;   s_out_size = 0; }

        jpeg_encode_memory_alloc_cfg_t in_mem_cfg = {
            .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER,
        };
        size_t alloc = 0;
        s_stage = jpeg_alloc_encoder_mem(stage_need, &in_mem_cfg, &alloc);
        s_stage_size = s_stage ? alloc : 0;

        jpeg_encode_memory_alloc_cfg_t out_mem_cfg = {
            .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
        };
        s_out = jpeg_alloc_encoder_mem(out_need, &out_mem_cfg, &alloc);
        s_out_size = s_out ? alloc : 0;

        if (!s_stage || !s_out) {
            FMRB_LOGE(TAG, "jpeg buffer alloc failed (%ux%u)", src_w, src_h);
            return FMRB_ERR_NO_MEMORY;
        }
    }
    if (pad_w != s_pad_w || src_h != s_src_h || src_w != s_src_w) {
        // The padding columns right of the picture must be black, and a
        // smaller frame reuses rows a larger one wrote into: clear it all.
        memset(s_stage, 0, s_stage_size);
        s_src_w = src_w;
        s_src_h = src_h;
        s_pad_w = pad_w;
        FMRB_LOGI(TAG, "JPEG encoder frame: %ux%u (padded %u)",
                  src_w, src_h, pad_w);
    }
    return FMRB_OK;
}

fmrb_err_t rd_encoder_jpeg_init(uint16_t src_w, uint16_t src_h, uint8_t quality)
{
    if (s_encoder) {
        // Already up: follow the frame size (the fullscreen high-resolution
        // mode changes it at runtime). The quality stays what the first
        // caller asked for; encode_q takes a per-call one.
        return ensure_buffers(src_w, src_h);
    }

    s_quality = quality;

    jpeg_encode_engine_cfg_t eng_cfg = {
        .timeout_ms = 100,
    };
    esp_err_t err = jpeg_new_encoder_engine(&eng_cfg, &s_encoder);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "jpeg encoder engine create failed: %d", err);
        return FMRB_ERR_FAILED;
    }

    fmrb_err_t ferr = ensure_buffers(src_w, src_h);
    if (ferr != FMRB_OK) {
        rd_encoder_jpeg_deinit();
        return ferr;
    }

    FMRB_LOGI(TAG, "JPEG encoder ready: %ux%u (padded %u) q=%u",
              src_w, src_h, s_pad_w, quality);
    return FMRB_OK;
}

void rd_encoder_jpeg_deinit(void)
{
    if (s_encoder) {
        jpeg_del_encoder_engine(s_encoder);
        s_encoder = NULL;
    }
    if (s_stage) { free(s_stage); s_stage = NULL; }
    if (s_out)   { free(s_out);   s_out = NULL; }
    s_stage_size = 0;
    s_out_size = 0;
    s_src_w = s_src_h = s_pad_w = 0;
}

uint16_t rd_encoder_jpeg_width(void)
{
    return s_pad_w;
}

fmrb_err_t rd_encoder_jpeg_encode(const uint16_t *pixels,
                                  const uint8_t **out_buf, size_t *out_len)
{
    return rd_encoder_jpeg_encode_q(pixels, s_quality, out_buf, out_len);
}

fmrb_err_t rd_encoder_jpeg_encode_q(const uint16_t *pixels, uint8_t quality,
                                    const uint8_t **out_buf, size_t *out_len)
{
    if (!s_encoder || !pixels || !out_buf || !out_len) {
        return FMRB_ERR_INVALID_PARAM;
    }

    // Row-pad 426 -> 432 (right edge stays black from ensure_buffers)
    const uint8_t *src = (const uint8_t *)pixels;
    for (int y = 0; y < s_src_h; y++) {
        memcpy(s_stage + (size_t)y * s_pad_w * 2,
               src + (size_t)y * s_src_w * 2,
               (size_t)s_src_w * 2);
    }

    jpeg_encode_cfg_t enc_cfg = {
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
        .image_quality = quality,
        .width = s_pad_w,
        .height = s_src_h,
    };

    uint32_t encoded = 0;
    esp_err_t err = jpeg_encoder_process(s_encoder, &enc_cfg,
                                         s_stage,
                                         (uint32_t)((size_t)s_pad_w * s_src_h * 2),
                                         s_out, (uint32_t)s_out_size,
                                         &encoded);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "jpeg encode failed: %d", err);
        return FMRB_ERR_FAILED;
    }
    *out_buf = s_out;
    *out_len = encoded;
    return FMRB_OK;
}
