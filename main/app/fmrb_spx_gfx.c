/**
 * @file fmrb_spx_gfx.c
 * @brief Implementation of the Spinel FmrbGfx FFI shim (fmrb_spx_gfx.h): what
 *        the app boundary (fmrb_boundary_gfx.c) does not carry yet.
 *
 * Each function reproduces the body of the matching mruby FmrbGfx method
 * (lib/gems/picoruby-fmrb-app/ports/esp32/gfx.c) minus the mrb_get_args /
 * mrb_raise plumbing: it packs a gfx_cmd_t and hands it to the same host_task
 * GFX queue, or calls the same fmrb_gfx_* / fmrb_transport_* primitives. The
 * receiver arrives as a plain int: the number of the FmrbGfx object's drawer
 * (fmrb_gfx_drawer.h), which has the canvas and the drawing origin, as for the
 * boundary's functions (doc/app_boundary W4b).
 */
#include "fmrb_spx_gfx.h"

#include <stdio.h>
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
#include "fmrb_kernel.h"
#include "fmrb_hal_time.h"
#include "fmrb_attr.h"
#include "fmrb_gfx_drawer.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_heap_caps.h"
#endif

/* The current instance's :binstr length (runtime sp_ctx.c; the host C
   cannot include sp_ctx.h, so it is declared here). */
int *sp_ctx_ffi_bin_len(void);

/* Submit a gfx_cmd_t and collapse the result to the shim convention. */
static int spx_gfx_submit(const gfx_cmd_t *cmd)
{
    return fmrb_gfx_submit(cmd) == FMRB_OK ? 0 : FMRB_SPX_ERR;
}

/* The drawer behind a receiver number, or a failure for the caller. */
#define SPX_DRAWER(num, d, fail) \
    const fmrb_gfx_drawer_t *d = fmrb_gfx_drawer_get(num); \
    if (!d) return (fail)
#define SPX_CANVAS(d) ((fmrb_canvas_handle_t)(d)->canvas)

/* ---- the drawer (FmrbGfx.new, _set_origin, destroy) ---------------------- */

int fmrb_spx_gfx_drawer_open(int canvas_id, int width, int height)
{
    return (int)fmrb_gfx_drawer_open(canvas_id, width, height);
}

int fmrb_spx_gfx_drawer_set_origin(int drawer, int x, int y, int w, int h)
{
    return (int)fmrb_gfx_drawer_set_origin(drawer, x, y, w, h);
}

int fmrb_spx_gfx_drawer_close(int drawer)
{
    fmrb_gfx_drawer_close(drawer);
    return 0;
}

/* The adaptors of the boundary functions that give back a record or bytes
   (generated from sig/, doc/app_boundary): Spinel takes those as :binstr. */
#include "fmrb_gen/fmrb_boundary_spx.inc"

/* ---- motion-JPEG playback ------------------------------------------------ */

static const char *spx_video_status_record(uint8_t cmd, const uint8_t *payload,
                                           size_t payload_len)
{
    FMRB_EXT_RAM_BSS_ATTR static uint8_t buf[FMRB_SPX_GFX_VIDEO_STATUS_RECORD_SIZE];
    *sp_ctx_ffi_bin_len() = 0;

#ifndef FMRB_HW_MODERN
    /* Retro cannot play video; answer now rather than after a sync timeout. */
    (void)cmd; (void)payload; (void)payload_len;
    return "";
#else

    uint8_t resp_buf[sizeof(fmrb_link_graphics_video_status_t)];
    uint32_t resp_len = sizeof(resp_buf);
    fmrb_err_t ret = fmrb_transport_send_sync(
        FMRB_LINK_TYPE_GRAPHICS, cmd,
        payload, payload_len,
        resp_buf, &resp_len, 5000);
    if (ret != FMRB_OK || resp_len < sizeof(fmrb_link_graphics_video_status_t)) {
        return "";
    }

    fmrb_link_graphics_video_status_t *st =
        (fmrb_link_graphics_video_status_t *)resp_buf;
    buf[0] = st->state;
    buf[1] = (uint8_t)(st->frames_shown & 0xFF);
    buf[2] = (uint8_t)((st->frames_shown >> 8) & 0xFF);
    buf[3] = (uint8_t)((st->frames_shown >> 16) & 0xFF);
    buf[4] = (uint8_t)((st->frames_shown >> 24) & 0xFF);
    buf[5] = (uint8_t)(st->frames_dropped & 0xFF);
    buf[6] = (uint8_t)((st->frames_dropped >> 8) & 0xFF);
    buf[7] = (uint8_t)((st->frames_dropped >> 16) & 0xFF);
    buf[8] = (uint8_t)((st->frames_dropped >> 24) & 0xFF);
    *sp_ctx_ffi_bin_len() = FMRB_SPX_GFX_VIDEO_STATUS_RECORD_SIZE;
    return (const char *)buf;
#endif  /* FMRB_HW_MODERN */
}

const char *fmrb_spx_gfx_video_open(int drawer, const char *path, int len,
                                    int x, int y, int fps, int loop)
{
    FMRB_EXT_RAM_BSS_ATTR static uint8_t buf[FMRB_SPX_GFX_VIDEO_INFO_RECORD_SIZE];
    *sp_ctx_ffi_bin_len() = 0;
    if (!path || len < 0 || len >= 120) {
        return "";
    }

#ifndef FMRB_HW_MODERN
    (void)drawer; (void)x; (void)y; (void)fps; (void)loop;
    return "";
#else
    SPX_DRAWER(drawer, d, "");
    /* a drawn coordinate: the origin, none into a sprite image */
    x += d->to_image ? 0 : d->ox;
    y += d->to_image ? 0 : d->oy;

    uint8_t payload_buf[sizeof(fmrb_link_graphics_video_open_t) + 120];
    size_t payload_len = sizeof(fmrb_link_graphics_video_open_t) + (size_t)len;
    fmrb_link_graphics_video_open_t *hdr =
        (fmrb_link_graphics_video_open_t *)payload_buf;
    hdr->canvas_id = (uint16_t)d->canvas;
    hdr->x         = (int16_t)x;
    hdr->y         = (int16_t)y;
    hdr->fps       = (uint16_t)fps;
    hdr->flags     = loop ? FMRB_LINK_GFX_VIDEO_FLAG_LOOP : 0;
    hdr->path_len  = (uint16_t)len;
    memcpy(payload_buf + sizeof(fmrb_link_graphics_video_open_t), path, (size_t)len);

    uint8_t resp_buf[sizeof(fmrb_link_graphics_video_opened_t)];
    uint32_t resp_len = sizeof(resp_buf);
    fmrb_err_t ret = fmrb_transport_send_sync(
        FMRB_LINK_TYPE_GRAPHICS,
        FMRB_LINK_GFX_VIDEO_OPEN,
        payload_buf, payload_len,
        resp_buf, &resp_len, 10000);
    if (ret != FMRB_OK || resp_len < sizeof(fmrb_link_graphics_video_opened_t)) {
        return "";
    }

    fmrb_link_graphics_video_opened_t *resp =
        (fmrb_link_graphics_video_opened_t *)resp_buf;
    if (resp->result != 0) {
        return "";  /* Ruby returns nil: the app can fall back */
    }
    buf[0] = (uint8_t)(resp->width & 0xFF);  buf[1] = (uint8_t)(resp->width >> 8);
    buf[2] = (uint8_t)(resp->height & 0xFF); buf[3] = (uint8_t)(resp->height >> 8);
    *sp_ctx_ffi_bin_len() = FMRB_SPX_GFX_VIDEO_INFO_RECORD_SIZE;
    return (const char *)buf;
#endif  /* FMRB_HW_MODERN */
}

const char *fmrb_spx_gfx_video_control(int action)
{
    fmrb_link_graphics_video_control_t payload;
    payload.action = (uint8_t)action;
    return spx_video_status_record(FMRB_LINK_GFX_VIDEO_CONTROL,
                                   (const uint8_t *)&payload, sizeof(payload));
}

const char *fmrb_spx_gfx_video_status(void)
{
    return spx_video_status_record(FMRB_LINK_GFX_VIDEO_STATUS, NULL, 0);
}
/* ---- sprite API --------------------------------------------------------- */

int fmrb_spx_gfx_create_sprite_image(int drawer, int width, int height,
                                     int trans_color, int use_trans)
{
    SPX_DRAWER(drawer, d, FMRB_SPX_ERR);
    fmrb_gfx_context_t gctx = fmrb_gfx_get_global_context();
    if (!gctx) {
        return FMRB_SPX_ERR;
    }
    uint16_t id = fmrb_gfx_create_sprite_image(gctx, SPX_CANVAS(d),
        (uint16_t)width, (uint16_t)height, (uint8_t)trans_color, use_trans != 0);
    return id == 0 ? FMRB_SPX_ERR : (int)id;
}

int fmrb_spx_gfx_load_sprite_image_bmp(int drawer, int image_id, const char *path, int len)
{
    SPX_DRAWER(drawer, d, FMRB_SPX_ERR);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_load_sprite_image_bmp_n(&cmd, SPX_CANVAS(d), (uint16_t)image_id, path,
                                         len > 0 ? (size_t)len : 0);
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_delete_sprite_image(int drawer, int image_id)
{
    SPX_DRAWER(drawer, d, FMRB_SPX_ERR);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_sprite_image(&cmd, SPX_CANVAS(d), (uint16_t)image_id);
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_set_sprite_image_target(int drawer, int image_id)
{
    SPX_DRAWER(drawer, d, FMRB_SPX_ERR);
    /* the drawer draws into the image (no origin) until the target is 0 */
    fmrb_gfx_drawer_set_to_image(drawer, image_id != 0);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_set_sprite_image_target(&cmd, SPX_CANVAS(d), (uint16_t)image_id);
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_create_sprite_instance(int drawer, const char *frames, int frame_count,
                                        int x, int y, int z_order)
{
    SPX_DRAWER(drawer, d, FMRB_SPX_ERR);
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
    uint16_t id = fmrb_gfx_create_sprite_instance(gctx, SPX_CANVAS(d),
        image_ids, (uint8_t)frame_count, (int16_t)(x + d->ox), (int16_t)(y + d->oy), (int16_t)z_order);
    return id == 0 ? FMRB_SPX_ERR : (int)id;
}

int fmrb_spx_gfx_delete_sprite_instance(int drawer, int instance_id)
{
    SPX_DRAWER(drawer, d, FMRB_SPX_ERR);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_sprite_instance(&cmd, SPX_CANVAS(d), (uint16_t)instance_id);
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_sprite_move(int drawer, int instance_id, int x, int y)
{
    SPX_DRAWER(drawer, d, FMRB_SPX_ERR);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_sprite_instance_move(&cmd, SPX_CANVAS(d), (uint16_t)instance_id,
                                      (int16_t)(x + d->ox), (int16_t)(y + d->oy));
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_sprite_visible(int drawer, int instance_id, int visible)
{
    SPX_DRAWER(drawer, d, FMRB_SPX_ERR);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_sprite_instance_set_visible(&cmd, SPX_CANVAS(d), (uint16_t)instance_id,
                                             visible != 0);
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_sprite_frame(int drawer, int instance_id, int frame_index)
{
    SPX_DRAWER(drawer, d, FMRB_SPX_ERR);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_sprite_instance_set_frame(&cmd, SPX_CANVAS(d), (uint16_t)instance_id,
                                           (uint8_t)frame_index);
    return spx_gfx_submit(&cmd);
}

int fmrb_spx_gfx_delete_all_sprites(int drawer)
{
    SPX_DRAWER(drawer, d, FMRB_SPX_ERR);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_all_sprites(&cmd, SPX_CANVAS(d));
    return spx_gfx_submit(&cmd);
}
