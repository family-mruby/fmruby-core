/**
 * @file fmrb_spx_gfx.c
 * @brief Implementation of the Spinel FmrbGfx FFI shim (fmrb_spx_gfx.h).
 *
 * Each function reproduces the body of the matching mruby FmrbGfx method
 * (lib/add/picoruby-fmrb-app/ports/esp32/gfx.c) minus the mrb_get_args /
 * mrb_raise plumbing: it packs a gfx_cmd_t and hands it to the same host_task
 * GFX queue, or calls the same fmrb_gfx_* / fmrb_transport_* primitives. The
 * canvas id arrives as a plain int (the Spinel FmrbGfx object stores it as an
 * instance variable instead of a boxed mrb_gfx_data).
 */
#include "fmrb_spx_gfx.h"

#include <string.h>
#include <stdint.h>
#include "fmrb_app.h"
#include "fmrb_gfx.h"
#include "fmrb_gfx_cmd.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_err.h"
#include "fmrb_msg.h"
#include "fmrb_mem.h"
#include "fmrb_rtos.h"
#include "fmrb_hal.h"
#include "fmrb_task_config.h"
#include "fmrb_link_protocol.h"
#include "fmrb_transport.h"
#include "fmrb_file_transfer_msg.h"

/* Byte length for :binstr FFI returns. Defined in fmrb_spx_kernel.c, which is
   compiled alongside this shim in every Spinel build. */
extern int sp_net_bin_len;

/* Submit a gfx_cmd_t and collapse the result to the shim convention. */
static int spx_gfx_submit(const gfx_cmd_t *cmd)
{
    return fmrb_gfx_submit(cmd) == FMRB_OK ? 0 : FMRB_SPX_ERR;
}

/* ---- basic drawing primitives ------------------------------------------- */

int fmrb_spx_gfx_clear(int canvas_id, int color)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_CLEAR,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.clear.color = (fmrb_color_t)color,
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_set_pixel(int canvas_id, int x, int y, int color)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_PIXEL,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.pixel = { .x = (int16_t)x, .y = (int16_t)y, .color = (fmrb_color_t)color },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_get_pixel(int canvas_id, int x, int y)
{
    fmrb_link_graphics_pixel_value_t resp = { .color = 0, .status = 0xFF };
    gfx_cmd_sync_ctx_t sync = {
        .done = fmrb_semaphore_create_binary(),
        .response_buf = (uint8_t *)&resp,
        .response_len = sizeof(resp),
        .result = -1,
    };
    if (!sync.done) {
        return FMRB_SPX_ERR;
    }

    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_GET_PIXEL,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.get_pixel = { .x = (int16_t)x, .y = (int16_t)y },
        .sync = &sync,
    };

    if (fmrb_gfx_submit(&cmd) != FMRB_OK) {
        fmrb_semaphore_delete(sync.done);
        return FMRB_SPX_ERR;
    }

    fmrb_base_type_t took = fmrb_semaphore_take(sync.done, FMRB_MS_TO_TICKS(5000));
    fmrb_semaphore_delete(sync.done);
    if (took != FMRB_TRUE) {
        return FMRB_SPX_ERR;
    }
    if (sync.result != 0 || sync.response_len < sizeof(resp) || resp.status == 0xFF) {
        return 0;  /* out-of-range / failed read -> 0 (matches mruby) */
    }
    return (int)resp.color;
}

int fmrb_spx_gfx_draw_line(int canvas_id, int x1, int y1, int x2, int y2, int color)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_LINE,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.line = { .x1 = (int16_t)x1, .y1 = (int16_t)y1,
                         .x2 = (int16_t)x2, .y2 = (int16_t)y2, .color = (fmrb_color_t)color },
    };
    return spx_gfx_submit(&cmd);
}

static int spx_gfx_rect(int canvas_id, int x, int y, int w, int h, int color, bool filled)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_RECT,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.rect = {
            .rect = { (int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h },
            .color = (fmrb_color_t)color,
            .filled = filled,
        },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_draw_rect(int canvas_id, int x, int y, int w, int h, int color)
{ return spx_gfx_rect(canvas_id, x, y, w, h, color, false); }

int fmrb_spx_gfx_fill_rect(int canvas_id, int x, int y, int w, int h, int color)
{ return spx_gfx_rect(canvas_id, x, y, w, h, color, true); }

int fmrb_spx_gfx_blend_rect(int canvas_id, int x, int y, int w, int h, int color, int mode)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_BLEND_RECT,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.blend_rect = {
            .rect = { (int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h },
            .color = (fmrb_color_t)color,
            .mode = (uint8_t)mode,
        },
    };
    return spx_gfx_submit(&cmd);
}

static int spx_gfx_circle(int canvas_id, int x, int y, int r, int color, bool filled)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_CIRCLE,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.circle = { .x = (int16_t)x, .y = (int16_t)y, .radius = (int16_t)r,
                           .color = (fmrb_color_t)color, .filled = filled },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_draw_circle(int canvas_id, int x, int y, int r, int color)
{ return spx_gfx_circle(canvas_id, x, y, r, color, false); }

int fmrb_spx_gfx_fill_circle(int canvas_id, int x, int y, int r, int color)
{ return spx_gfx_circle(canvas_id, x, y, r, color, true); }

static int spx_gfx_round_rect(int canvas_id, int x, int y, int w, int h, int r, int color, bool filled)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_ROUND_RECT,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.round_rect = { .x = (int16_t)x, .y = (int16_t)y, .w = (int16_t)w, .h = (int16_t)h,
                               .radius = (int16_t)r, .color = (fmrb_color_t)color, .filled = filled },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_draw_round_rect(int canvas_id, int x, int y, int w, int h, int r, int color)
{ return spx_gfx_round_rect(canvas_id, x, y, w, h, r, color, false); }

int fmrb_spx_gfx_fill_round_rect(int canvas_id, int x, int y, int w, int h, int r, int color)
{ return spx_gfx_round_rect(canvas_id, x, y, w, h, r, color, true); }

static int spx_gfx_ellipse(int canvas_id, int x, int y, int rx, int ry, int color, bool filled)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_ELLIPSE,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.ellipse = { .x = (int16_t)x, .y = (int16_t)y, .rx = (int16_t)rx, .ry = (int16_t)ry,
                            .color = (fmrb_color_t)color, .filled = filled },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_draw_ellipse(int canvas_id, int x, int y, int rx, int ry, int color)
{ return spx_gfx_ellipse(canvas_id, x, y, rx, ry, color, false); }

int fmrb_spx_gfx_fill_ellipse(int canvas_id, int x, int y, int rx, int ry, int color)
{ return spx_gfx_ellipse(canvas_id, x, y, rx, ry, color, true); }

static int spx_gfx_triangle(int canvas_id, int x0, int y0, int x1, int y1, int x2, int y2, int color, bool filled)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_TRIANGLE,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.triangle = { .x0 = (int16_t)x0, .y0 = (int16_t)y0, .x1 = (int16_t)x1, .y1 = (int16_t)y1,
                             .x2 = (int16_t)x2, .y2 = (int16_t)y2, .color = (fmrb_color_t)color, .filled = filled },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_draw_triangle(int canvas_id, int x0, int y0, int x1, int y1, int x2, int y2, int color)
{ return spx_gfx_triangle(canvas_id, x0, y0, x1, y1, x2, y2, color, false); }

int fmrb_spx_gfx_fill_triangle(int canvas_id, int x0, int y0, int x1, int y1, int x2, int y2, int color)
{ return spx_gfx_triangle(canvas_id, x0, y0, x1, y1, x2, y2, color, true); }

static int spx_gfx_arc(int canvas_id, int x, int y, int r0, int r1, int a0, int a1, int color, bool filled)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_ARC,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.arc = { .x = (int16_t)x, .y = (int16_t)y, .r0 = (int16_t)r0, .r1 = (int16_t)r1,
                        .angle0 = (int16_t)a0, .angle1 = (int16_t)a1,
                        .color = (fmrb_color_t)color, .filled = filled },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_draw_arc(int canvas_id, int x, int y, int r0, int r1, int angle0, int angle1, int color)
{ return spx_gfx_arc(canvas_id, x, y, r0, r1, angle0, angle1, color, false); }

int fmrb_spx_gfx_fill_arc(int canvas_id, int x, int y, int r0, int r1, int angle0, int angle1, int color)
{ return spx_gfx_arc(canvas_id, x, y, r0, r1, angle0, angle1, color, true); }

/* ---- text --------------------------------------------------------------- */

int fmrb_spx_gfx_set_text_size(int canvas_id, int size)
{
    if (size < 1) size = 1;
    if (size > 4) size = 4;
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_TEXT_SIZE,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.text_size = { .size = (uint8_t)size },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_set_font(int canvas_id, int family, int size)
{
    uint8_t fam;
    if (family == 1) {
        fam = FMRB_LINK_GFX_FONT_FAMILY_JA;
        if (size != 8 && size != 12) {
            return FMRB_SPX_ERR_RANGE;  /* only 8/12 supported for :ja */
        }
    } else {
        fam = FMRB_LINK_GFX_FONT_FAMILY_DEFAULT;
    }
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_SET_FONT,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.set_font = { .family = fam, .size = (uint8_t)size },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_draw_text(int canvas_id, int x, int y, const char *text, int len,
                           int color, int bg_color, int flags)
{
    if (!text || len < 0) {
        return FMRB_SPX_ERR_RANGE;
    }
    bool bg_given = (flags & 0x1) != 0;
    uint8_t hybrid = (flags & 0x2) ? 1 : 0;

    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_TEXT,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.text = {
            .x = (int16_t)x,
            .y = (int16_t)y,
            .color = (fmrb_color_t)color,
            .bg_color = (fmrb_color_t)bg_color,
            .bg_transparent = !bg_given,
            .font_size = FMRB_FONT_SIZE_MEDIUM,
            .hybrid_mode = hybrid,
        },
    };
    size_t cap = sizeof(cmd.params.text.text) - 1;
    size_t n = ((size_t)len < cap) ? (size_t)len : cap;
    memcpy(cmd.params.text.text, text, n);
    cmd.params.text.text[n] = '\0';
    return spx_gfx_submit(&cmd);
}

/* ---- present ------------------------------------------------------------ */

int fmrb_spx_gfx_present(int canvas_id, int x, int y, int explicit_pos)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return FMRB_SPX_ERR;
    }
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_PRESENT,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.present = {
            .x = explicit_pos ? (int16_t)x : (int16_t)ctx->window_pos_x,
            .y = explicit_pos ? (int16_t)y : (int16_t)ctx->window_pos_y,
            .transparent_color = 0xFF,
        },
    };
    return spx_gfx_submit(&cmd);
}

/* ---- CVBS/NTSC output control ------------------------------------------- */

int fmrb_spx_gfx_set_output_level(int canvas_id, int level)
{
    if (level < 0) level = 0;
    if (level > 255) level = 255;
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_SET_OUTPUT_LEVEL,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.set_output_level = { .level = (uint8_t)level },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_set_chroma_level(int canvas_id, int level)
{
    if (level < 0) level = 0;
    if (level > 255) level = 255;
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_SET_CHROMA_LEVEL,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.set_chroma_level = { .level = (uint8_t)level },
    };
    return spx_gfx_submit(&cmd);
}

/* ---- composite regions / viewport --------------------------------------- */

int fmrb_spx_gfx_set_composite_regions(int canvas_id, const char *packed, int count)
{
    if (count < 0 || count > FMRB_GFX_MAX_COMPOSITE_REGIONS) {
        return FMRB_SPX_ERR_RANGE;
    }
    if (count > 0 && !packed) {
        return FMRB_SPX_ERR_RANGE;
    }
    fmrb_gfx_context_t gctx = fmrb_gfx_get_global_context();
    if (!gctx) {
        return FMRB_SPX_ERR;
    }

    fmrb_gfx_composite_region_t regions[FMRB_GFX_MAX_COMPOSITE_REGIONS];
    for (int i = 0; i < count; i++) {
        const uint8_t *r = (const uint8_t *)packed + (size_t)i * FMRB_SPX_GFX_REGION_RECORD_SIZE;
        int16_t f[7];
        for (int k = 0; k < 7; k++) {
            f[k] = (int16_t)((uint16_t)r[k * 2] | ((uint16_t)r[k * 2 + 1] << 8));
        }
        regions[i].src_x = f[0];
        regions[i].src_y = f[1];
        regions[i].dst_x = f[2];
        regions[i].dst_y = f[3];
        regions[i].w     = f[4];
        regions[i].h     = f[5];
        regions[i].use_transparent = (uint8_t)(f[6] ? 1 : 0);
    }
    fmrb_gfx_set_composite_regions(gctx, (fmrb_canvas_handle_t)canvas_id, regions, (uint8_t)count);
    return 0;  /* best-effort, like the mruby binding */
}

int fmrb_spx_gfx_set_canvas_viewport(int canvas_id, int src_x, int src_y, int view_w, int view_h)
{
    fmrb_gfx_context_t gctx = fmrb_gfx_get_global_context();
    if (!gctx) {
        return FMRB_SPX_ERR;
    }
    fmrb_gfx_set_canvas_viewport(gctx, (fmrb_canvas_handle_t)canvas_id,
                                 (uint16_t)src_x, (uint16_t)src_y,
                                 (uint16_t)view_w, (uint16_t)view_h);
    return 0;
}

/* ---- image API ---------------------------------------------------------- */

int fmrb_spx_gfx_draw_image(int canvas_id, int image_id, int x, int y,
                            int scale_x_fp8, int scale_y_fp8)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_DRAW_IMAGE,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.draw_image = {
            .image_id = (uint16_t)image_id,
            .x = (int16_t)x,
            .y = (int16_t)y,
            .flags = 0,
            .scale_x_fp8 = (int16_t)scale_x_fp8,
            .scale_y_fp8 = (int16_t)scale_y_fp8,
        },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_delete_image(int canvas_id, int image_id)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_DELETE_IMAGE,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.delete_image = { .image_id = (uint16_t)image_id },
    };
    return spx_gfx_submit(&cmd);
}

const char *fmrb_spx_gfx_create_image_from_file(int canvas_id, const char *path, int len)
{
    static uint8_t buf[FMRB_SPX_GFX_IMAGE_INFO_RECORD_SIZE];
    sp_net_bin_len = 0;
    if (!path || len < 0 || len >= 120) {
        return "";
    }

    uint8_t payload_buf[sizeof(fmrb_link_graphics_create_image_from_file_t) + 120];
    size_t payload_len = sizeof(fmrb_link_graphics_create_image_from_file_t) + (size_t)len;
    fmrb_link_graphics_create_image_from_file_t *hdr =
        (fmrb_link_graphics_create_image_from_file_t *)payload_buf;
    hdr->canvas_id = (uint16_t)canvas_id;
    hdr->path_len = (uint16_t)len;
    memcpy(payload_buf + sizeof(fmrb_link_graphics_create_image_from_file_t), path, (size_t)len);

    uint8_t resp_buf[sizeof(fmrb_link_graphics_image_created_t)];
    uint32_t resp_len = sizeof(resp_buf);
    fmrb_err_t ret = fmrb_transport_send_sync(
        FMRB_LINK_TYPE_GRAPHICS,
        FMRB_LINK_GFX_CREATE_IMAGE_FROM_FILE,
        payload_buf, payload_len,
        resp_buf, &resp_len,
        10000);
    if (ret != FMRB_OK || resp_len < sizeof(fmrb_link_graphics_image_created_t)) {
        return "";  /* Ruby returns nil (matches mruby: broken image != fatal) */
    }

    fmrb_link_graphics_image_created_t *resp = (fmrb_link_graphics_image_created_t *)resp_buf;
    buf[0] = (uint8_t)(resp->image_id & 0xFF); buf[1] = (uint8_t)(resp->image_id >> 8);
    buf[2] = (uint8_t)(resp->width & 0xFF);    buf[3] = (uint8_t)(resp->width >> 8);
    buf[4] = (uint8_t)(resp->height & 0xFF);   buf[5] = (uint8_t)(resp->height >> 8);
    sp_net_bin_len = FMRB_SPX_GFX_IMAGE_INFO_RECORD_SIZE;
    return (const char *)buf;
}

/* Mask upload chunking (identical to gfx.c). */
#define SPX_MASK_CHUNK_OVERHEAD 18
#define SPX_MASK_CHUNK_SIZE \
    (FMRB_LINK_FRAME_MAX_DATA - sizeof(fmrb_link_graphics_mask_data_t) - SPX_MASK_CHUNK_OVERHEAD)

int fmrb_spx_gfx_create_mask(int canvas_id, int width, int height, const char *data, int len)
{
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024 || !data) {
        return FMRB_SPX_ERR_RANGE;
    }
    size_t expected = (size_t)(((width + 7) / 8) * height);
    if ((size_t)len < expected) {
        return FMRB_SPX_ERR_RANGE;
    }

    fmrb_link_graphics_create_mask_t begin = {
        .canvas_id = (uint16_t)canvas_id,
        .width = (uint16_t)width,
        .height = (uint16_t)height,
    };
    fmrb_link_graphics_mask_created_t resp = { .mask_id = 0 };
    uint32_t resp_len = sizeof(resp);
    fmrb_err_t ret = fmrb_transport_send_sync(
        FMRB_LINK_TYPE_GRAPHICS, FMRB_LINK_GFX_CREATE_MASK,
        (const uint8_t *)&begin, sizeof(begin),
        (uint8_t *)&resp, &resp_len, 5000);
    if (ret != FMRB_OK || resp_len < sizeof(resp) || resp.mask_id == 0) {
        return FMRB_SPX_ERR;
    }
    uint16_t mask_id = resp.mask_id;

    uint8_t chunk_buf[FMRB_LINK_FRAME_MAX_DATA];
    size_t offset = 0;
    while (offset < expected) {
        size_t remaining = expected - offset;
        size_t chunk_len = (remaining > SPX_MASK_CHUNK_SIZE) ? SPX_MASK_CHUNK_SIZE : remaining;
        fmrb_link_graphics_mask_data_t *hdr = (fmrb_link_graphics_mask_data_t *)chunk_buf;
        hdr->mask_id = mask_id;
        hdr->offset = (uint32_t)offset;
        hdr->chunk_len = (uint16_t)chunk_len;
        memcpy(chunk_buf + sizeof(*hdr), data + offset, chunk_len);
        ret = fmrb_transport_send(FMRB_LINK_TYPE_GRAPHICS, FMRB_LINK_GFX_MASK_DATA,
                                  chunk_buf, sizeof(*hdr) + chunk_len,
                                  FMRB_TRANSPORT_TIMEOUT_DEFAULT);
        if (ret != FMRB_OK) {
            return FMRB_SPX_ERR;
        }
        offset += chunk_len;
    }
    return (int)mask_id;
}

int fmrb_spx_gfx_delete_mask(int canvas_id, int mask_id)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_DELETE_MASK,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.delete_mask = { .mask_id = (uint16_t)mask_id },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_draw_image_masked(int canvas_id, int image_id, int mask_id, int x, int y)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_DRAW_IMAGE_MASKED,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.draw_image_masked = {
            .image_id = (uint16_t)image_id,
            .mask_id = (uint16_t)mask_id,
            .x = (int16_t)x,
            .y = (int16_t)y,
        },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_draw_tile(int canvas_id, int image_id, int src_x, int src_y,
                           int w, int h, int dst_x, int dst_y)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_DRAW_TILE,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.draw_tile = {
            .image_id = (uint16_t)image_id,
            .src_x = (int16_t)src_x, .src_y = (int16_t)src_y,
            .w = (uint16_t)w, .h = (uint16_t)h,
            .dst_x = (int16_t)dst_x, .dst_y = (int16_t)dst_y,
        },
    };
    return spx_gfx_submit(&cmd);
}

/* ---- file transfer ------------------------------------------------------ */

/* Send a file_cmd_t to host_task and block for its result (mirrors gfx.c). */
static fmrb_err_t spx_file_cmd_sync(file_cmd_t *cmd, file_cmd_result_t *result, uint32_t timeout_ms)
{
    result->done_sem = fmrb_semaphore_create_binary();
    if (!result->done_sem) {
        return FMRB_ERR_NO_MEMORY;
    }
    result->result = -99;
    cmd->result = result;

    fmrb_msg_t msg = {
        .type = FMRB_MSG_TYPE_FILE_TRANSFER,
        .src_pid = 0,
        .size = sizeof(file_cmd_t),
    };
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (ctx) {
        msg.src_pid = ctx->app_id;
    }
    memcpy(msg.data, cmd, sizeof(file_cmd_t));

    fmrb_err_t ret = fmrb_msg_send(PROC_ID_HOST, &msg, 5000);
    if (ret != FMRB_OK) {
        fmrb_semaphore_delete(result->done_sem);
        return ret;
    }
    fmrb_base_type_t sem_ret = fmrb_semaphore_take(result->done_sem, FMRB_MS_TO_TICKS(timeout_ms));
    fmrb_semaphore_delete(result->done_sem);
    return (sem_ret == FMRB_PASS) ? FMRB_OK : FMRB_ERR_TIMEOUT;
}

int fmrb_spx_gfx_transfer_file(const char *src, int slen, const char *dst, int dlen)
{
    (void)slen;
    if (!src || !dst || dlen <= 0 || dlen >= 120) {
        return FMRB_SPX_ERR_RANGE;
    }
    char path[256];
    char dest[120];
    size_t sn = (slen > 0 && (size_t)slen < sizeof(path)) ? (size_t)slen : 0;
    if (sn == 0) {
        return FMRB_SPX_ERR_RANGE;
    }
    memcpy(path, src, sn); path[sn] = '\0';
    memcpy(dest, dst, (size_t)dlen); dest[dlen] = '\0';

    fmrb_file_info_t info;
    if (fmrb_hal_file_stat(path, &info) != FMRB_OK) {
        return FMRB_SPX_ERR;
    }
    uint32_t file_size = (uint32_t)info.size;
    if (file_size == 0) {
        return FMRB_SPX_ERR;
    }
    uint8_t *file_data = (uint8_t *)fmrb_sys_malloc(file_size);
    if (!file_data) {
        return FMRB_SPX_ERR;
    }
    fmrb_file_t fh;
    if (fmrb_hal_file_open(path, FMRB_O_RDONLY, &fh) != FMRB_OK) {
        fmrb_sys_free(file_data);
        return FMRB_SPX_ERR;
    }
    size_t bytes_read = 0;
    fmrb_err_t rret = fmrb_hal_file_read(fh, file_data, file_size, &bytes_read);
    fmrb_hal_file_close(fh);
    if (rret != FMRB_OK || bytes_read != file_size) {
        fmrb_sys_free(file_data);
        return FMRB_SPX_ERR;
    }

    file_cmd_t cmd = {0};
    file_cmd_result_t result = {0};
    cmd.cmd_type = FILE_CMD_TRANSFER;
    cmd.path_len = (uint16_t)dlen;
    memcpy(cmd.path, dest, (size_t)dlen);
    cmd.params.transfer.data = file_data;
    cmd.params.transfer.data_len = file_size;

    fmrb_err_t ret = spx_file_cmd_sync(&cmd, &result, 30000);
    if (ret != FMRB_OK) {
        /* on send failure we still own the buffer */
        fmrb_sys_free(file_data);
        return FMRB_SPX_ERR;
    }
    if (result.result != 0) {
        return FMRB_SPX_ERR;
    }
    return 1;
}

int fmrb_spx_gfx_file_status(const char *path, int len)
{
    if (!path || len <= 0 || len >= 120) {
        return FMRB_SPX_ERR_RANGE;
    }
    file_cmd_result_t result;
    result.done_sem = fmrb_semaphore_create_binary();
    if (!result.done_sem) {
        return FMRB_SPX_ERR;
    }
    result.result = -1;
    memset(&result.data, 0, sizeof(result.data));

    fmrb_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = FMRB_MSG_TYPE_FILE_TRANSFER;
    msg.size = sizeof(file_cmd_t);
    file_cmd_t *cmd = (file_cmd_t *)msg.data;
    cmd->cmd_type = FILE_CMD_STATUS;
    cmd->result = &result;
    cmd->path_len = (uint16_t)len;
    memcpy(cmd->path, path, (size_t)len);

    if (fmrb_msg_send(PROC_ID_HOST, &msg, 5000) != FMRB_OK) {
        fmrb_semaphore_delete(result.done_sem);
        return FMRB_SPX_ERR;
    }
    fmrb_base_type_t wait_ret = fmrb_semaphore_take(result.done_sem, FMRB_MS_TO_TICKS(5000));
    fmrb_semaphore_delete(result.done_sem);
    if (wait_ret != FMRB_TRUE || result.result != 0) {
        return FMRB_SPX_ERR;
    }
    if (!result.data.status.exists) {
        return FMRB_SPX_ERR;  /* Ruby maps this to exists=false */
    }
    return (int)result.data.status.file_size;
}

/* ---- sprite API --------------------------------------------------------- */

int fmrb_spx_gfx_create_sprite_image(int canvas_id, int width, int height,
                                     int trans_color, int use_trans)
{
    fmrb_gfx_context_t gctx = fmrb_gfx_get_global_context();
    if (!gctx) {
        return FMRB_SPX_ERR;
    }
    uint16_t id = fmrb_gfx_create_sprite_image(gctx, (fmrb_canvas_handle_t)canvas_id,
        (uint16_t)width, (uint16_t)height, (uint8_t)trans_color, use_trans != 0);
    return id == 0 ? FMRB_SPX_ERR : (int)id;
}

int fmrb_spx_gfx_load_sprite_image_bmp(int canvas_id, int image_id, const char *path, int len)
{
    gfx_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd_type = GFX_CMD_LOAD_SPRITE_IMAGE_BMP;
    cmd.canvas_id = (fmrb_canvas_handle_t)canvas_id;
    cmd.params.load_sprite_image_bmp.image_id = (uint16_t)image_id;
    size_t cap = sizeof(cmd.params.load_sprite_image_bmp.path) - 1;
    size_t n = (path && len > 0) ? (((size_t)len < cap) ? (size_t)len : cap) : 0;
    if (n) {
        memcpy(cmd.params.load_sprite_image_bmp.path, path, n);
    }
    cmd.params.load_sprite_image_bmp.path[n] = '\0';
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_delete_sprite_image(int canvas_id, int image_id)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_DELETE_SPRITE_IMAGE,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.delete_sprite_image = { .image_id = (uint16_t)image_id },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_set_sprite_image_target(int canvas_id, int image_id)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_SET_SPRITE_IMAGE_TARGET,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.set_sprite_image_target = { .image_id = (uint16_t)image_id },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_create_sprite_instance(int canvas_id, const char *frames, int frame_count,
                                        int x, int y, int z_order)
{
    if (frame_count <= 0 || frame_count > FMRB_SPRITE_MAX_FRAMES || !frames) {
        return FMRB_SPX_ERR_RANGE;
    }
    fmrb_gfx_context_t gctx = fmrb_gfx_get_global_context();
    if (!gctx) {
        return FMRB_SPX_ERR;
    }
    uint16_t image_ids[FMRB_SPRITE_MAX_FRAMES];
    const uint8_t *p = (const uint8_t *)frames;
    for (int i = 0; i < frame_count; i++) {
        image_ids[i] = (uint16_t)((uint16_t)p[i * 2] | ((uint16_t)p[i * 2 + 1] << 8));
    }
    uint16_t id = fmrb_gfx_create_sprite_instance(gctx, (fmrb_canvas_handle_t)canvas_id,
        image_ids, (uint8_t)frame_count, (int16_t)x, (int16_t)y, (int16_t)z_order);
    return id == 0 ? FMRB_SPX_ERR : (int)id;
}

int fmrb_spx_gfx_delete_sprite_instance(int canvas_id, int instance_id)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_DELETE_SPRITE_INSTANCE,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.delete_sprite_instance = { .instance_id = (uint16_t)instance_id },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_sprite_move(int canvas_id, int instance_id, int x, int y)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_SPRITE_INSTANCE_MOVE,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.sprite_instance_move = {
            .instance_id = (uint16_t)instance_id, .x = (int16_t)x, .y = (int16_t)y },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_sprite_visible(int canvas_id, int instance_id, int visible)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_SPRITE_INSTANCE_SET_VISIBLE,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.sprite_instance_set_visible = {
            .instance_id = (uint16_t)instance_id, .visible = visible ? 1 : 0 },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_sprite_frame(int canvas_id, int instance_id, int frame_index)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_SPRITE_INSTANCE_SET_FRAME,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
        .params.sprite_instance_set_frame = {
            .instance_id = (uint16_t)instance_id, .frame_index = (uint8_t)frame_index },
    };
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_delete_all_sprites(int canvas_id)
{
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_DELETE_ALL_SPRITES,
        .canvas_id = (fmrb_canvas_handle_t)canvas_id,
    };
    return spx_gfx_submit(&cmd);
}
