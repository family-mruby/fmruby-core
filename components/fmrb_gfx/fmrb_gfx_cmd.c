#include "fmrb_gfx_cmd.h"

#include <string.h>

#include "fmrb_app.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"

static const char *TAG = "gfx_cmd";

// Host queue flow control. Owned by the host task, registered below.
static fmrb_semaphore_t g_flow_semaphore = NULL;

void fmrb_gfx_set_flow_semaphore(fmrb_semaphore_t sem)
{
    g_flow_semaphore = sem;
}

fmrb_err_t fmrb_gfx_submit(const gfx_cmd_t *cmd)
{
    if (!cmd) {
        return FMRB_ERR_INVALID_PARAM;
    }

    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        FMRB_LOGE(TAG, "No app context for graphics command");
        return FMRB_ERR_INVALID_STATE;
    }

    // Reserving a slot keeps FMRB_HOST_HID_RESERVED_SLOTS free for input.
    fmrb_semaphore_t sem = g_flow_semaphore;
    if (sem) {
        // Wait as long as it takes: the queue draining is the only thing that
        // can make room, and dropping the command instead would lose drawing.
        if (fmrb_semaphore_take(sem, UINT32_MAX) != FMRB_PASS) {
            FMRB_LOGE(TAG, "Failed to acquire the graphics queue semaphore");
            return FMRB_ERR_TIMEOUT;
        }
    }

    fmrb_msg_t msg = {
        .type = FMRB_MSG_TYPE_APP_GFX,
        .src_pid = ctx->app_id,
        .size = sizeof(gfx_cmd_t)
    };
    memcpy(msg.data, cmd, sizeof(gfx_cmd_t));

    fmrb_err_t ret = fmrb_msg_send(PROC_ID_HOST, &msg, 5000);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to send graphics command: %d", ret);
        if (sem) {
            fmrb_semaphore_give(sem);  // give back the slot we never used
        }
    }
    // On success the host task releases the semaphore once it has the command.
    return ret;
}

/* ---- command constructors ---------------------------------------------- */

// Every constructor starts here, so no field of the outgoing command is left
// holding whatever was on the caller's stack - the sync pointer above all.
static void cmd_begin(gfx_cmd_t *cmd, gfx_cmd_type_t type,
                      fmrb_canvas_handle_t canvas_id)
{
    memset(cmd, 0, sizeof(*cmd));
    cmd->cmd_type = type;
    cmd->canvas_id = canvas_id;
}

// Copy src_len bytes into a fixed command buffer, truncating to fit.
static void cmd_copy_bytes(char *dst, size_t dst_size, const char *src,
                           size_t src_len)
{
    if (dst_size == 0) {
        return;
    }
    size_t cap = dst_size - 1;
    size_t len = (!src) ? 0 : (src_len < cap ? src_len : cap);
    if (len) {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
}

void fmrb_gfx_cmd_clear(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                        fmrb_color_t color)
{
    cmd_begin(cmd, GFX_CMD_CLEAR, canvas_id);
    cmd->params.clear.color = color;
}

void fmrb_gfx_cmd_pixel(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                        int16_t x, int16_t y, fmrb_color_t color)
{
    cmd_begin(cmd, GFX_CMD_PIXEL, canvas_id);
    cmd->params.pixel.x = x;
    cmd->params.pixel.y = y;
    cmd->params.pixel.color = color;
}

void fmrb_gfx_cmd_get_pixel(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                            int16_t x, int16_t y,
                            struct gfx_cmd_sync_ctx *sync)
{
    cmd_begin(cmd, GFX_CMD_GET_PIXEL, canvas_id);
    cmd->params.get_pixel.x = x;
    cmd->params.get_pixel.y = y;
    cmd->sync = sync;
}

void fmrb_gfx_cmd_line(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                       int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                       fmrb_color_t color)
{
    cmd_begin(cmd, GFX_CMD_LINE, canvas_id);
    cmd->params.line.x1 = x1;
    cmd->params.line.y1 = y1;
    cmd->params.line.x2 = x2;
    cmd->params.line.y2 = y2;
    cmd->params.line.color = color;
}

void fmrb_gfx_cmd_rect(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                       int16_t x, int16_t y, uint16_t w, uint16_t h,
                       fmrb_color_t color, bool filled)
{
    cmd_begin(cmd, GFX_CMD_RECT, canvas_id);
    cmd->params.rect.rect.x = x;
    cmd->params.rect.rect.y = y;
    cmd->params.rect.rect.width = w;
    cmd->params.rect.rect.height = h;
    cmd->params.rect.color = color;
    cmd->params.rect.filled = filled;
}

void fmrb_gfx_cmd_blend_rect(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                             int16_t x, int16_t y, uint16_t w, uint16_t h,
                             fmrb_color_t color, uint8_t mode)
{
    cmd_begin(cmd, GFX_CMD_BLEND_RECT, canvas_id);
    cmd->params.blend_rect.rect.x = x;
    cmd->params.blend_rect.rect.y = y;
    cmd->params.blend_rect.rect.width = w;
    cmd->params.blend_rect.rect.height = h;
    cmd->params.blend_rect.color = color;
    cmd->params.blend_rect.mode = mode;
}

void fmrb_gfx_cmd_round_rect(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                             int16_t x, int16_t y, int16_t w, int16_t h,
                             int16_t radius, fmrb_color_t color, bool filled)
{
    cmd_begin(cmd, GFX_CMD_ROUND_RECT, canvas_id);
    cmd->params.round_rect.x = x;
    cmd->params.round_rect.y = y;
    cmd->params.round_rect.w = w;
    cmd->params.round_rect.h = h;
    cmd->params.round_rect.radius = radius;
    cmd->params.round_rect.color = color;
    cmd->params.round_rect.filled = filled;
}

void fmrb_gfx_cmd_circle(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                         int16_t x, int16_t y, int16_t radius,
                         fmrb_color_t color, bool filled)
{
    cmd_begin(cmd, GFX_CMD_CIRCLE, canvas_id);
    cmd->params.circle.x = x;
    cmd->params.circle.y = y;
    cmd->params.circle.radius = radius;
    cmd->params.circle.color = color;
    cmd->params.circle.filled = filled;
}

void fmrb_gfx_cmd_ellipse(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                          int16_t x, int16_t y, int16_t rx, int16_t ry,
                          fmrb_color_t color, bool filled)
{
    cmd_begin(cmd, GFX_CMD_ELLIPSE, canvas_id);
    cmd->params.ellipse.x = x;
    cmd->params.ellipse.y = y;
    cmd->params.ellipse.rx = rx;
    cmd->params.ellipse.ry = ry;
    cmd->params.ellipse.color = color;
    cmd->params.ellipse.filled = filled;
}

void fmrb_gfx_cmd_triangle(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                           int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                           int16_t x2, int16_t y2, fmrb_color_t color,
                           bool filled)
{
    cmd_begin(cmd, GFX_CMD_TRIANGLE, canvas_id);
    cmd->params.triangle.x0 = x0;
    cmd->params.triangle.y0 = y0;
    cmd->params.triangle.x1 = x1;
    cmd->params.triangle.y1 = y1;
    cmd->params.triangle.x2 = x2;
    cmd->params.triangle.y2 = y2;
    cmd->params.triangle.color = color;
    cmd->params.triangle.filled = filled;
}

void fmrb_gfx_cmd_arc(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                      int16_t x, int16_t y, int16_t r0, int16_t r1,
                      int16_t angle0, int16_t angle1, fmrb_color_t color,
                      bool filled)
{
    cmd_begin(cmd, GFX_CMD_ARC, canvas_id);
    cmd->params.arc.x = x;
    cmd->params.arc.y = y;
    cmd->params.arc.r0 = r0;
    cmd->params.arc.r1 = r1;
    cmd->params.arc.angle0 = angle0;
    cmd->params.arc.angle1 = angle1;
    cmd->params.arc.color = color;
    cmd->params.arc.filled = filled;
}

void fmrb_gfx_cmd_text_n(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                         int16_t x, int16_t y, const char *text,
                         size_t text_len, fmrb_color_t color,
                         fmrb_color_t bg_color, bool bg_transparent,
                         fmrb_font_size_t font_size, uint8_t hybrid_mode)
{
    cmd_begin(cmd, GFX_CMD_TEXT, canvas_id);
    cmd->params.text.x = x;
    cmd->params.text.y = y;
    cmd->params.text.color = color;
    cmd->params.text.bg_color = bg_color;
    cmd->params.text.bg_transparent = bg_transparent;
    cmd->params.text.font_size = font_size;
    cmd->params.text.hybrid_mode = hybrid_mode;
    cmd_copy_bytes(cmd->params.text.text, sizeof(cmd->params.text.text), text,
                   text_len);
}

void fmrb_gfx_cmd_text(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                       int16_t x, int16_t y, const char *text,
                       fmrb_color_t color, fmrb_color_t bg_color,
                       bool bg_transparent, fmrb_font_size_t font_size,
                       uint8_t hybrid_mode)
{
    fmrb_gfx_cmd_text_n(cmd, canvas_id, x, y, text, text ? strlen(text) : 0,
                        color, bg_color, bg_transparent, font_size,
                        hybrid_mode);
}

void fmrb_gfx_cmd_text_size(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                            uint8_t size)
{
    cmd_begin(cmd, GFX_CMD_TEXT_SIZE, canvas_id);
    cmd->params.text_size.size = size;
}

void fmrb_gfx_cmd_set_font(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                           uint8_t family, uint8_t size)
{
    cmd_begin(cmd, GFX_CMD_SET_FONT, canvas_id);
    cmd->params.set_font.family = family;
    cmd->params.set_font.size = size;
}

void fmrb_gfx_cmd_present(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                          int16_t x, int16_t y, fmrb_color_t transparent_color)
{
    cmd_begin(cmd, GFX_CMD_PRESENT, canvas_id);
    cmd->params.present.x = x;
    cmd->params.present.y = y;
    cmd->params.present.transparent_color = transparent_color;
}

void fmrb_gfx_cmd_draw_image(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                             uint16_t image_id, int16_t x, int16_t y,
                             uint8_t flags, int16_t scale_x_fp8,
                             int16_t scale_y_fp8)
{
    cmd_begin(cmd, GFX_CMD_DRAW_IMAGE, canvas_id);
    cmd->params.draw_image.image_id = image_id;
    cmd->params.draw_image.x = x;
    cmd->params.draw_image.y = y;
    cmd->params.draw_image.flags = flags;
    cmd->params.draw_image.scale_x_fp8 = scale_x_fp8;
    cmd->params.draw_image.scale_y_fp8 = scale_y_fp8;
}

void fmrb_gfx_cmd_draw_image_masked(gfx_cmd_t *cmd,
                                    fmrb_canvas_handle_t canvas_id,
                                    uint16_t image_id, uint16_t mask_id,
                                    int16_t x, int16_t y)
{
    cmd_begin(cmd, GFX_CMD_DRAW_IMAGE_MASKED, canvas_id);
    cmd->params.draw_image_masked.image_id = image_id;
    cmd->params.draw_image_masked.mask_id = mask_id;
    cmd->params.draw_image_masked.x = x;
    cmd->params.draw_image_masked.y = y;
}

void fmrb_gfx_cmd_draw_tile(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                            uint16_t image_id, int16_t src_x, int16_t src_y,
                            uint16_t w, uint16_t h, int16_t dst_x,
                            int16_t dst_y)
{
    cmd_begin(cmd, GFX_CMD_DRAW_TILE, canvas_id);
    cmd->params.draw_tile.image_id = image_id;
    cmd->params.draw_tile.src_x = src_x;
    cmd->params.draw_tile.src_y = src_y;
    cmd->params.draw_tile.w = w;
    cmd->params.draw_tile.h = h;
    cmd->params.draw_tile.dst_x = dst_x;
    cmd->params.draw_tile.dst_y = dst_y;
}

void fmrb_gfx_cmd_delete_image(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                               uint16_t image_id)
{
    cmd_begin(cmd, GFX_CMD_DELETE_IMAGE, canvas_id);
    cmd->params.delete_image.image_id = image_id;
}

void fmrb_gfx_cmd_delete_mask(gfx_cmd_t *cmd, fmrb_canvas_handle_t canvas_id,
                              uint16_t mask_id)
{
    cmd_begin(cmd, GFX_CMD_DELETE_MASK, canvas_id);
    cmd->params.delete_mask.mask_id = mask_id;
}

void fmrb_gfx_cmd_set_output_level(gfx_cmd_t *cmd,
                                   fmrb_canvas_handle_t canvas_id,
                                   uint8_t level)
{
    cmd_begin(cmd, GFX_CMD_SET_OUTPUT_LEVEL, canvas_id);
    cmd->params.set_output_level.level = level;
}

void fmrb_gfx_cmd_set_chroma_level(gfx_cmd_t *cmd,
                                   fmrb_canvas_handle_t canvas_id,
                                   uint8_t level)
{
    cmd_begin(cmd, GFX_CMD_SET_CHROMA_LEVEL, canvas_id);
    cmd->params.set_chroma_level.level = level;
}

void fmrb_gfx_cmd_load_sprite_image_bmp_n(gfx_cmd_t *cmd,
                                          fmrb_canvas_handle_t canvas_id,
                                          uint16_t image_id, const char *path,
                                          size_t path_len)
{
    cmd_begin(cmd, GFX_CMD_LOAD_SPRITE_IMAGE_BMP, canvas_id);
    cmd->params.load_sprite_image_bmp.image_id = image_id;
    cmd_copy_bytes(cmd->params.load_sprite_image_bmp.path,
                   sizeof(cmd->params.load_sprite_image_bmp.path), path,
                   path_len);
}

void fmrb_gfx_cmd_load_sprite_image_bmp(gfx_cmd_t *cmd,
                                        fmrb_canvas_handle_t canvas_id,
                                        uint16_t image_id, const char *path)
{
    fmrb_gfx_cmd_load_sprite_image_bmp_n(cmd, canvas_id, image_id, path,
                                         path ? strlen(path) : 0);
}

void fmrb_gfx_cmd_delete_sprite_image(gfx_cmd_t *cmd,
                                      fmrb_canvas_handle_t canvas_id,
                                      uint16_t image_id)
{
    cmd_begin(cmd, GFX_CMD_DELETE_SPRITE_IMAGE, canvas_id);
    cmd->params.delete_sprite_image.image_id = image_id;
}

void fmrb_gfx_cmd_set_sprite_image_target(gfx_cmd_t *cmd,
                                          fmrb_canvas_handle_t canvas_id,
                                          uint16_t image_id)
{
    cmd_begin(cmd, GFX_CMD_SET_SPRITE_IMAGE_TARGET, canvas_id);
    cmd->params.set_sprite_image_target.image_id = image_id;
}

void fmrb_gfx_cmd_delete_sprite_instance(gfx_cmd_t *cmd,
                                         fmrb_canvas_handle_t canvas_id,
                                         uint16_t instance_id)
{
    cmd_begin(cmd, GFX_CMD_DELETE_SPRITE_INSTANCE, canvas_id);
    cmd->params.delete_sprite_instance.instance_id = instance_id;
}

void fmrb_gfx_cmd_sprite_instance_move(gfx_cmd_t *cmd,
                                       fmrb_canvas_handle_t canvas_id,
                                       uint16_t instance_id, int16_t x,
                                       int16_t y)
{
    cmd_begin(cmd, GFX_CMD_SPRITE_INSTANCE_MOVE, canvas_id);
    cmd->params.sprite_instance_move.instance_id = instance_id;
    cmd->params.sprite_instance_move.x = x;
    cmd->params.sprite_instance_move.y = y;
}

void fmrb_gfx_cmd_sprite_instance_set_visible(gfx_cmd_t *cmd,
                                              fmrb_canvas_handle_t canvas_id,
                                              uint16_t instance_id,
                                              bool visible)
{
    cmd_begin(cmd, GFX_CMD_SPRITE_INSTANCE_SET_VISIBLE, canvas_id);
    cmd->params.sprite_instance_set_visible.instance_id = instance_id;
    cmd->params.sprite_instance_set_visible.visible = visible ? 1 : 0;
}

void fmrb_gfx_cmd_sprite_instance_set_frame(gfx_cmd_t *cmd,
                                            fmrb_canvas_handle_t canvas_id,
                                            uint16_t instance_id,
                                            uint8_t frame_index)
{
    cmd_begin(cmd, GFX_CMD_SPRITE_INSTANCE_SET_FRAME, canvas_id);
    cmd->params.sprite_instance_set_frame.instance_id = instance_id;
    cmd->params.sprite_instance_set_frame.frame_index = frame_index;
}

void fmrb_gfx_cmd_delete_all_sprites(gfx_cmd_t *cmd,
                                     fmrb_canvas_handle_t canvas_id)
{
    cmd_begin(cmd, GFX_CMD_DELETE_ALL_SPRITES, canvas_id);
}
