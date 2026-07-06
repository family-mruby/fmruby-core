// P4 hardware H.264 encoder wrapper (remote desktop Phase 2).
//
// Design originally assumed the HW encoder was YUV-only (PPA conversion
// step); the shipped esp_h264 v1.1.x HW encoder accepts RGB565_LE
// directly, so no color conversion is needed. The framebuffer is RGB565
// non-swapped little-endian (PPA-native), which matches RGB565_LE -
// verify colors on device and revisit if red/blue swap.
//
// IDR-on-demand: esp_h264 exposes no direct force-IDR call; the GOP
// parameter is temporarily set to 1 so the next frame becomes an IDR,
// then restored. IDR frames carry SPS/PPS automatically.

#include "rd_encoder_h264.h"

#include "fmrb_log.h"
#include "esp_h264_enc_single_hw.h"
#include "esp_h264_alloc.h"

#include <string.h>
#include <stdatomic.h>

static const char *TAG = "rd_h264";

static esp_h264_enc_handle_t s_enc = NULL;
static esp_h264_enc_param_hw_handle_t s_param = NULL;
static rd_h264_config_t s_cfg;
static uint16_t s_pad_w = 0;
static uint8_t *s_in_buf = NULL;
static uint32_t s_in_len = 0;
static uint8_t *s_out_buf = NULL;
static uint32_t s_out_len = 0;
static atomic_bool s_want_idr = false;
static bool s_gop_forced = false;

fmrb_err_t rd_encoder_h264_init(const rd_h264_config_t *cfg)
{
    if (s_enc) return FMRB_OK;
    if (!cfg) return FMRB_ERR_INVALID_PARAM;
    s_cfg = *cfg;
    s_pad_w = (uint16_t)((cfg->src_w + 15u) & ~15u);

    esp_h264_enc_cfg_hw_t enc_cfg = {
        .pic_type = ESP_H264_RAW_FMT_RGB565_LE,
        .gop = cfg->gop,
        .fps = cfg->fps,
        .res = { .width = s_pad_w, .height = cfg->src_h },
        .rc = { .bitrate = cfg->bitrate, .qp_min = 25, .qp_max = 48 },
    };
    esp_h264_err_t err = esp_h264_enc_hw_new(&enc_cfg, &s_enc);
    if (err != ESP_H264_ERR_OK) {
        FMRB_LOGE(TAG, "enc_hw_new failed: %d", err);
        return FMRB_ERR_FAILED;
    }
    if (esp_h264_enc_hw_get_param_hd(s_enc, &s_param) != ESP_H264_ERR_OK) {
        s_param = NULL;  // IDR-on-demand degrades to GOP wait
    }
    err = esp_h264_enc_open(s_enc);
    if (err != ESP_H264_ERR_OK) {
        FMRB_LOGE(TAG, "enc_open failed: %d", err);
        esp_h264_enc_del(s_enc);
        s_enc = NULL;
        return FMRB_ERR_FAILED;
    }

    // Input: padded RGB565 frame; output: same byte budget (encoded frames
    // are far smaller, this guards against ESP_H264_ERR_MEM)
    uint32_t raw = (uint32_t)s_pad_w * s_cfg.src_h * 2;
    s_in_buf = esp_h264_aligned_calloc(16, 1, raw, &s_in_len,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_out_buf = esp_h264_aligned_calloc(16, 1, raw, &s_out_len,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_in_buf || !s_out_buf) {
        FMRB_LOGE(TAG, "buffer alloc failed");
        rd_encoder_h264_deinit();
        return FMRB_ERR_NO_MEMORY;
    }

    FMRB_LOGI(TAG, "H.264 encoder ready: %ux%u (padded %u) %ufps gop=%u %ukbps",
              cfg->src_w, cfg->src_h, s_pad_w, cfg->fps, cfg->gop,
              (unsigned)(cfg->bitrate / 1000));
    return FMRB_OK;
}

void rd_encoder_h264_deinit(void)
{
    if (s_enc) {
        esp_h264_enc_close(s_enc);
        esp_h264_enc_del(s_enc);
        s_enc = NULL;
        s_param = NULL;
    }
    if (s_in_buf)  { free(s_in_buf);  s_in_buf = NULL; }
    if (s_out_buf) { free(s_out_buf); s_out_buf = NULL; }
}

uint16_t rd_encoder_h264_width(void)
{
    return s_pad_w;
}

void rd_encoder_h264_request_idr(void)
{
    atomic_store(&s_want_idr, true);
}

fmrb_err_t rd_encoder_h264_encode(const uint16_t *pixels, uint32_t pts_ms,
                                  const uint8_t **out_buf, size_t *out_len,
                                  bool *out_is_idr)
{
    if (!s_enc || !pixels || !out_buf || !out_len) return FMRB_ERR_INVALID_PARAM;

    // IDR on demand: shrink the GOP to 1 for one frame
    if (s_param) {
        if (atomic_exchange(&s_want_idr, false)) {
            esp_h264_enc_set_gop(&s_param->base, 1);
            s_gop_forced = true;
        } else if (s_gop_forced) {
            esp_h264_enc_set_gop(&s_param->base, s_cfg.gop);
            s_gop_forced = false;
        }
    }

    // Row-pad the 426px source into the 432px encoder input
    const uint8_t *src = (const uint8_t *)pixels;
    for (int y = 0; y < s_cfg.src_h; y++) {
        memcpy(s_in_buf + (size_t)y * s_pad_w * 2,
               src + (size_t)y * s_cfg.src_w * 2,
               (size_t)s_cfg.src_w * 2);
    }

    esp_h264_enc_in_frame_t in = {
        .raw_data = { .buffer = s_in_buf, .len = s_in_len },
        .pts = pts_ms,
    };
    esp_h264_enc_out_frame_t out = {
        .raw_data = { .buffer = s_out_buf, .len = s_out_len },
    };
    esp_h264_err_t err = esp_h264_enc_process(s_enc, &in, &out);
    if (err != ESP_H264_ERR_OK) {
        FMRB_LOGE(TAG, "encode failed: %d", err);
        return FMRB_ERR_FAILED;
    }

    *out_buf = s_out_buf;
    *out_len = out.length;
    if (out_is_idr) {
        *out_is_idr = (out.frame_type == ESP_H264_FRAME_TYPE_IDR ||
                       out.frame_type == ESP_H264_FRAME_TYPE_I);
    }
    return FMRB_OK;
}
