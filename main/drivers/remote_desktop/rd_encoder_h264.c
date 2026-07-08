// P4 hardware H.264 encoder wrapper (remote desktop Phase 2).
//
// The esp_h264 header advertises RGB565_LE input for the HW encoder, but
// that path is gated on CONFIG_ESP_REV_MIN_FULL >= 300 (chip rev >= v3.0).
// On the Tab5 (chip rev v1.0) esp_h264_enc_hw_new() rejects RGB565_LE at
// runtime, so the original design applies: the PPA SRM engine converts
// each RGB565 capture frame to the O_UYY_E_VYY YUV420 layout (the P4
// 2D-DMA packed YUV420 format), padding 426 -> 432 in the same pass.
//
// IDR-on-demand: esp_h264 exposes no direct force-IDR call; the GOP
// parameter is temporarily set to 1 so the next frame becomes an IDR,
// then restored. IDR frames carry SPS/PPS automatically.

#include "rd_encoder_h264.h"

#include "fmrb_log.h"
#include "esp_h264_enc_single_hw.h"
#include "esp_h264_alloc.h"
#include "driver/ppa.h"

#include <string.h>
#include <stdatomic.h>

static const char *TAG = "rd_h264";

static esp_h264_enc_handle_t s_enc = NULL;
static esp_h264_enc_param_hw_handle_t s_param = NULL;
static ppa_client_handle_t s_ppa = NULL;
static rd_h264_config_t s_cfg;
static uint16_t s_pad_w = 0;
static uint8_t *s_in_buf = NULL;    // O_UYY_E_VYY YUV420, pad_w x src_h
static uint32_t s_in_len = 0;
static uint8_t *s_out_buf = NULL;
static uint32_t s_out_len = 0;
static atomic_bool s_want_idr = false;
static bool s_gop_forced = false;

// Fill the YUV420 buffer with black (limited range: Y=16, chroma=128).
// O_UYY_E_VYY packs each line as 3-byte groups: even lines U Y Y,
// odd lines V Y Y. Only the 426..431 column padding stays this color;
// the PPA writes the visible block every frame.
static void yuv420_fill_black(uint8_t *buf, uint32_t w, uint32_t h)
{
    size_t line_bytes = (size_t)w * 3 / 2;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *line = buf + (size_t)y * line_bytes;
        for (size_t i = 0; i < line_bytes; i += 3) {
            line[i]     = 128;  // U (even lines) / V (odd lines)
            line[i + 1] = 16;   // Y
            line[i + 2] = 16;   // Y
        }
    }
}

fmrb_err_t rd_encoder_h264_init(const rd_h264_config_t *cfg)
{
    if (s_enc) return FMRB_OK;
    if (!cfg) return FMRB_ERR_INVALID_PARAM;
    s_cfg = *cfg;
    s_pad_w = (uint16_t)((cfg->src_w + 15u) & ~15u);

    esp_h264_enc_cfg_hw_t enc_cfg = {
        .pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY,
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

    // PPA SRM client for the RGB565 -> YUV420 color conversion
    ppa_client_config_t ppa_cfg = {
        .oper_type = PPA_OPERATION_SRM,
    };
    if (ppa_register_client(&ppa_cfg, &s_ppa) != ESP_OK) {
        FMRB_LOGE(TAG, "ppa_register_client failed");
        rd_encoder_h264_deinit();
        return FMRB_ERR_FAILED;
    }

    // Input: padded O_UYY_E_VYY frame (1.5 B/px). 128-byte alignment
    // satisfies both the H.264 DMA and the PPA output cache-line rules.
    // Output: raw RGB565 byte budget (encoded frames are far smaller,
    // this guards against ESP_H264_ERR_MEM).
    uint32_t yuv = (uint32_t)s_pad_w * s_cfg.src_h * 3 / 2;
    uint32_t raw = (uint32_t)s_pad_w * s_cfg.src_h * 2;
    s_in_buf = esp_h264_aligned_calloc(128, 1, yuv, &s_in_len,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_out_buf = esp_h264_aligned_calloc(16, 1, raw, &s_out_len,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_in_buf || !s_out_buf) {
        FMRB_LOGE(TAG, "buffer alloc failed");
        rd_encoder_h264_deinit();
        return FMRB_ERR_NO_MEMORY;
    }
    yuv420_fill_black(s_in_buf, s_pad_w, s_cfg.src_h);

    FMRB_LOGI(TAG, "H.264 encoder ready: %ux%u (padded %u, PPA yuv420) %ufps gop=%u %ukbps",
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
    if (s_ppa) {
        ppa_unregister_client(s_ppa);
        s_ppa = NULL;
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
    if (!s_enc || !s_ppa || !pixels || !out_buf || !out_len) {
        return FMRB_ERR_INVALID_PARAM;
    }

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

    // RGB565 -> O_UYY_E_VYY YUV420 via PPA, writing the 426-wide block
    // into the 432-wide padded picture (right padding stays black)
    ppa_srm_oper_config_t srm = {
        .in = {
            .buffer = pixels,
            .pic_w = s_cfg.src_w,
            .pic_h = s_cfg.src_h,
            .block_w = s_cfg.src_w,
            .block_h = s_cfg.src_h,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = s_in_buf,
            .buffer_size = s_in_len,
            .pic_w = s_pad_w,
            .pic_h = s_cfg.src_h,
            .srm_cm = PPA_SRM_COLOR_MODE_YUV420,
            .yuv_range = PPA_COLOR_RANGE_LIMIT,
            .yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .alpha_update_mode = PPA_ALPHA_NO_CHANGE,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    esp_err_t perr = ppa_do_scale_rotate_mirror(s_ppa, &srm);
    if (perr != ESP_OK) {
        FMRB_LOGE(TAG, "ppa srm convert failed: %d", perr);
        return FMRB_ERR_FAILED;
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
