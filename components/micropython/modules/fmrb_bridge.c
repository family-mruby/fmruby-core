/**
 * Firmware side of the _fmrb module.
 *
 * Everything here that needs an fmruby-core or ESP-IDF header lives on this
 * side of fmrb_mp_bridge.h, so fmrb_module.c stays preprocessable by the qstr
 * extractor (see modules/micropython.mk).
 *
 * The drawing calls build the same gfx_cmd_t the mruby binding builds and hand
 * it to the same fmrb_gfx_submit(), semaphore and all, so a Python app competes
 * for graphics bandwidth on the same terms as every other app.
 */

#include <string.h>

#include "fmrb_app.h"
#include "fmrb_err.h"
#include "fmrb_app_canvas.h"
#include "fmrb_hal_file.h"
#include "fmrb_kernel.h"
#include "fmrb_link_protocol.h"
#include "fmrb_transport.h"
#include "fmrb_gfx.h"
#include "fmrb_gfx_cmd.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"
#include "fmrb_rtos.h"

#include "fmrb_mp_bridge.h"

static const char *TAG = "fmrb_mp_mod";

// Longest path an image command carries (the link payload sizes both).
#define FMRB_MP_IMAGE_PATH_MAX (120)
// Decoding a PNG on the graphics side is the slow part of create_image.
#define FMRB_MP_IMAGE_DECODE_TIMEOUT_MS (10000)

// fmrb_module.c cannot include fmrb_msg.h (the qstr extractor preprocesses it
// without the firmware headers), so it carries its own copies of these. Check
// them here, where both definitions are visible.
_Static_assert(FMRB_MP_MSG_BUF_SIZE == FMRB_MAX_MSG_PAYLOAD_SIZE,
               "fmrb_module.c message buffer no longer matches fmrb_msg.h");
_Static_assert(FMRB_MP_MSG_TYPE_APP_CONTROL == FMRB_MSG_TYPE_APP_CONTROL,
               "fmrb_module.c APP_CONTROL type no longer matches fmrb_msg.h");
_Static_assert(FMRB_MP_MSG_TYPE_HID_EVENT == FMRB_MSG_TYPE_HID_EVENT,
               "fmrb_module.c HID_EVENT type no longer matches fmrb_msg.h");

int fmrb_mp_bridge_app_init(fmrb_mp_app_info_t *out) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx || !out) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->name = ctx->app_name;
    out->canvas_id = -1;
    out->bg_canvas_id = -1;
    out->window_width = ctx->window_width;
    out->window_height = ctx->window_height;
    out->pos_x = ctx->window_pos_x;
    out->pos_y = ctx->window_pos_y;
    out->fullscreen = ctx->fullscreen;
    out->rounded_corners = !ctx->fullscreen && ctx->rounded_corners;
    out->headless = ctx->headless;
#ifdef CONFIG_IDF_TARGET_LINUX
    out->is_esp32 = false;
#else
    out->is_esp32 = true;
#endif

    FMRB_LOGI(TAG, "_init: app=%s id=%d %dx%d at (%d,%d) fs=%d headless=%d",
              ctx->app_name, (int)ctx->app_id,
              (int)ctx->window_width, (int)ctx->window_height,
              (int)ctx->window_pos_x, (int)ctx->window_pos_y,
              (int)ctx->fullscreen, (int)ctx->headless);

    if (ctx->headless) {
        return 0;
    }

    fmrb_canvas_handle_t canvas_id = FMRB_CANVAS_SCREEN;
    fmrb_canvas_handle_t bg_id = FMRB_CANVAS_SCREEN;
    if (fmrb_app_canvas_init(ctx, &canvas_id, &bg_id) != FMRB_OK) {
        return -1;
    }
    out->canvas_id = (int32_t)canvas_id;
    if (bg_id != FMRB_CANVAS_SCREEN) {
        out->bg_canvas_id = (int32_t)bg_id;
    }
    return 0;
}

void fmrb_mp_bridge_app_cleanup(void) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return;
    }
    FMRB_LOGI(TAG, "_cleanup: app_id=%d, name=%s", (int)ctx->app_id, ctx->app_name);

    fmrb_app_canvas_release_all(ctx);
    fmrb_msg_delete_queue(ctx->app_id);
}

bool fmrb_mp_bridge_should_exit(void) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    return ctx ? ctx->should_exit : true;
}

uint32_t fmrb_mp_bridge_now_ms(void) {
    return (uint32_t)(fmrb_task_get_tick_count() * portTICK_PERIOD_MS);
}

int fmrb_mp_bridge_recv(uint8_t *buf, size_t buf_size, int *out_type,
                        uint32_t timeout_ms) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx || !buf) {
        return -1;
    }

    // fmrb_msg_receive takes milliseconds (it applies FMRB_MS_TO_TICKS
    // internally); pass timeout_ms straight through.
    fmrb_msg_t msg;
    if (fmrb_msg_receive(ctx->app_id, &msg, timeout_ms) != FMRB_OK) {
        return -1;
    }

    uint32_t n = msg.size;
    if (n > buf_size) {
        n = (uint32_t)buf_size;
    }
    memcpy(buf, msg.data, n);
    if (out_type) {
        *out_type = (int)msg.type;
    }
    return (int)n;
}

void fmrb_mp_bridge_note_control(int msg_type, const uint8_t *payload, uint32_t size) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (ctx) {
        fmrb_app_note_control_payload(ctx, (uint8_t)msg_type, payload, size);
    }
}

bool fmrb_mp_bridge_send(int dest_pid, int msg_type, const uint8_t *data, uint32_t size) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx || size > FMRB_MAX_MSG_PAYLOAD_SIZE) {
        return false;
    }

    fmrb_msg_t msg = {
        .type = (uint8_t)msg_type,
        .src_pid = ctx->app_id,
        .size = size
    };
    if (size > 0 && data) {
        memcpy(msg.data, data, size);
    }
    return fmrb_msg_send((fmrb_proc_id_t)dest_pid, &msg, 1000) == FMRB_OK;
}

void fmrb_mp_bridge_set_window_pos(int32_t x, int32_t y) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return;
    }
    ctx->window_pos_x = (uint16_t)x;
    ctx->window_pos_y = (uint16_t)y;
}

bool fmrb_mp_bridge_is_file_app(void) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    return ctx && ctx->load_mode == FMRB_LOAD_MODE_FILE && ctx->filepath[0] != '\0';
}

int fmrb_mp_gfx_clear(int canvas_id, int color) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_clear(&cmd, (fmrb_canvas_handle_t)canvas_id, (fmrb_color_t)color);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_set_pixel(int canvas_id, int x, int y, int color) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_pixel(&cmd, (fmrb_canvas_handle_t)canvas_id, (int16_t)x, (int16_t)y,
                       (fmrb_color_t)color);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_draw_line(int canvas_id, int x0, int y0, int x1, int y1, int color) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_line(&cmd, (fmrb_canvas_handle_t)canvas_id, (int16_t)x0, (int16_t)y0,
                      (int16_t)x1, (int16_t)y1, (fmrb_color_t)color);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_rect(int canvas_id, int x, int y, int w, int h, int color, bool filled) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_rect(&cmd, (fmrb_canvas_handle_t)canvas_id, (int16_t)x, (int16_t)y,
                      (uint16_t)w, (uint16_t)h, (fmrb_color_t)color, filled);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_circle(int canvas_id, int x, int y, int r, int color, bool filled) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_circle(&cmd, (fmrb_canvas_handle_t)canvas_id, (int16_t)x, (int16_t)y,
                        (int16_t)r, (fmrb_color_t)color, filled);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_round_rect(int canvas_id, int x, int y, int w, int h, int r, int color,
                           bool filled) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_round_rect(&cmd, (fmrb_canvas_handle_t)canvas_id, (int16_t)x, (int16_t)y,
                            (int16_t)w, (int16_t)h, (int16_t)r, (fmrb_color_t)color,
                            filled);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_ellipse(int canvas_id, int x, int y, int rx, int ry, int color, bool filled) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_ellipse(&cmd, (fmrb_canvas_handle_t)canvas_id, (int16_t)x, (int16_t)y,
                         (int16_t)rx, (int16_t)ry, (fmrb_color_t)color, filled);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_triangle(int canvas_id, int x0, int y0, int x1, int y1, int x2, int y2,
                         int color, bool filled) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_triangle(&cmd, (fmrb_canvas_handle_t)canvas_id, (int16_t)x0, (int16_t)y0,
                          (int16_t)x1, (int16_t)y1, (int16_t)x2, (int16_t)y2,
                          (fmrb_color_t)color, filled);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_draw_text(int canvas_id, int x, int y, const char *text, int color,
                          int bg_color, bool has_bg, bool mixed) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_text(&cmd, (fmrb_canvas_handle_t)canvas_id, (int16_t)x, (int16_t)y, text,
                      (fmrb_color_t)color, (fmrb_color_t)bg_color, !has_bg,
                      FMRB_FONT_SIZE_MEDIUM, mixed ? 1 : 0);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_set_font(int canvas_id, int family, int size) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_set_font(&cmd, (fmrb_canvas_handle_t)canvas_id, (uint8_t)family,
                          (uint8_t)size);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_set_text_size(int canvas_id, int size) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_text_size(&cmd, (fmrb_canvas_handle_t)canvas_id, (uint8_t)size);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

/* ---------------------------------------------------------------------------
 * Images
 * ------------------------------------------------------------------------ */

bool fmrb_mp_gfx_sync_file(const char *src, const char *dest) {
    if (!src || !dest) {
        return false;
    }
    return fmrb_kernel_sync_file(src, dest) == FMRB_OK;
}

int fmrb_mp_gfx_create_image(int canvas_id, const char *path, uint16_t *out_id,
                             uint16_t *out_width, uint16_t *out_height) {
    if (!path || !out_id) {
        return -1;
    }
    size_t path_len = strlen(path);
    if (path_len >= FMRB_MP_IMAGE_PATH_MAX) {
        return -1;
    }

    // Sent straight down the transport rather than through the host task: the
    // answer carries the image id, and host_task cannot wait for a reply it is
    // itself responsible for pumping.
    uint8_t payload[sizeof(fmrb_link_graphics_create_image_from_file_t)
                    + FMRB_MP_IMAGE_PATH_MAX];
    fmrb_link_graphics_create_image_from_file_t *hdr =
        (fmrb_link_graphics_create_image_from_file_t *)payload;
    hdr->canvas_id = (uint16_t)canvas_id;
    hdr->path_len = (uint16_t)path_len;
    memcpy(payload + sizeof(*hdr), path, path_len);

    uint8_t resp_buf[sizeof(fmrb_link_graphics_image_created_t)];
    uint32_t resp_len = sizeof(resp_buf);
    fmrb_err_t ret = fmrb_transport_send_sync(
        FMRB_LINK_TYPE_GRAPHICS, FMRB_LINK_GFX_CREATE_IMAGE_FROM_FILE,
        payload, sizeof(*hdr) + path_len, resp_buf, &resp_len,
        FMRB_MP_IMAGE_DECODE_TIMEOUT_MS);
    if (ret != FMRB_OK || resp_len < sizeof(fmrb_link_graphics_image_created_t)) {
        FMRB_LOGE(TAG, "create_image failed: %d (path=%s)", (int)ret, path);
        return -1;
    }

    const fmrb_link_graphics_image_created_t *resp =
        (const fmrb_link_graphics_image_created_t *)resp_buf;
    *out_id = resp->image_id;
    if (out_width) {
        *out_width = resp->width;
    }
    if (out_height) {
        *out_height = resp->height;
    }
    return 0;
}

int fmrb_mp_gfx_draw_image(int canvas_id, int image_id, int x, int y, int scale_x_fp8,
                           int scale_y_fp8) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_draw_image(&cmd, (fmrb_canvas_handle_t)canvas_id, (uint16_t)image_id,
                            (int16_t)x, (int16_t)y, 0, (int16_t)scale_x_fp8,
                            (int16_t)scale_y_fp8);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_delete_image(int canvas_id, int image_id) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_image(&cmd, (fmrb_canvas_handle_t)canvas_id, (uint16_t)image_id);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_draw_tile(int canvas_id, int image_id, int src_x, int src_y, int w,
                          int h, int dst_x, int dst_y) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_draw_tile(&cmd, (fmrb_canvas_handle_t)canvas_id, (uint16_t)image_id,
                           (int16_t)src_x, (int16_t)src_y, (uint16_t)w, (uint16_t)h,
                           (int16_t)dst_x, (int16_t)dst_y);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

/* ---------------------------------------------------------------------------
 * Sprites
 * ------------------------------------------------------------------------ */

_Static_assert(FMRB_MP_SPRITE_MAX_FRAMES == FMRB_SPRITE_MAX_FRAMES,
               "fmrb_module.c sprite frame limit no longer matches the protocol");

int fmrb_mp_gfx_create_sprite_image(int canvas_id, int width, int height,
                                    int transparent_color, bool use_transparent) {
    fmrb_gfx_context_t gfx = fmrb_gfx_get_global_context();
    if (!gfx) {
        return 0;
    }
    return (int)fmrb_gfx_create_sprite_image(gfx, (uint16_t)canvas_id, (uint16_t)width,
                                             (uint16_t)height,
                                             (uint8_t)transparent_color,
                                             use_transparent);
}

int fmrb_mp_gfx_load_sprite_image_bmp(int canvas_id, int image_id, const char *path) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_load_sprite_image_bmp(&cmd, (fmrb_canvas_handle_t)canvas_id,
                                       (uint16_t)image_id, path);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_delete_sprite_image(int canvas_id, int image_id) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_sprite_image(&cmd, (fmrb_canvas_handle_t)canvas_id,
                                     (uint16_t)image_id);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_set_sprite_target(int canvas_id, int image_id) {
    // Through the queue, not straight to the graphics context: the switch has
    // to stay in order with the drawing around it, or the tail of a sprite
    // draw lands on the canvas instead of in the image.
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_set_sprite_image_target(&cmd, (fmrb_canvas_handle_t)canvas_id,
                                         (uint16_t)image_id);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_create_sprite_instance(int canvas_id, const uint16_t *image_ids,
                                       int frame_count, int x, int y, int z_order) {
    fmrb_gfx_context_t gfx = fmrb_gfx_get_global_context();
    if (!gfx || !image_ids || frame_count <= 0
        || frame_count > FMRB_SPRITE_MAX_FRAMES) {
        return 0;
    }
    return (int)fmrb_gfx_create_sprite_instance(gfx, (uint16_t)canvas_id, image_ids,
                                                (uint8_t)frame_count, (int16_t)x,
                                                (int16_t)y, (int16_t)z_order);
}

int fmrb_mp_gfx_sprite_move(int canvas_id, int instance_id, int x, int y) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_sprite_instance_move(&cmd, (fmrb_canvas_handle_t)canvas_id,
                                      (uint16_t)instance_id, (int16_t)x, (int16_t)y);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_sprite_visible(int canvas_id, int instance_id, bool visible) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_sprite_instance_set_visible(&cmd, (fmrb_canvas_handle_t)canvas_id,
                                             (uint16_t)instance_id, visible);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_sprite_frame(int canvas_id, int instance_id, int frame_index) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_sprite_instance_set_frame(&cmd, (fmrb_canvas_handle_t)canvas_id,
                                           (uint16_t)instance_id,
                                           (uint8_t)frame_index);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_delete_sprite_instance(int canvas_id, int instance_id) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_sprite_instance(&cmd, (fmrb_canvas_handle_t)canvas_id,
                                        (uint16_t)instance_id);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

int fmrb_mp_gfx_delete_all_sprites(int canvas_id) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_all_sprites(&cmd, (fmrb_canvas_handle_t)canvas_id);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

bool fmrb_mp_bridge_audio_note(bool on, int channel, int freq, int volume,
                               int duty, int sweep) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return false;
    }
    // The shared builder in main/app/fmrb_app.c, so a note from Python is
    // byte for byte the note the Ruby binding and the MIDI scheduler send.
    return fmrb_app_send_audio_note((int)ctx->app_id, on, channel, freq, volume,
                                    duty, sweep, 1000) == FMRB_OK;
}

const char *fmrb_mp_bridge_language(void) {
    const fmrb_system_config_t *cfg = fmrb_kernel_get_config();
    return (cfg && cfg->language[0]) ? cfg->language : "en";
}

int fmrb_mp_gfx_present(int canvas_id, int x, int y, bool explicit_pos) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return -1;
    }
    // 0xFF = no transparency; the canvas colour key handles rounded corners.
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_present(&cmd, (fmrb_canvas_handle_t)canvas_id,
                         explicit_pos ? (int16_t)x : (int16_t)ctx->window_pos_x,
                         explicit_pos ? (int16_t)y : (int16_t)ctx->window_pos_y, 0xFF);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
}

fmrb_mp_path_kind_t fmrb_mp_bridge_path_kind(const char *path) {
    fmrb_file_info_t info;
    if (!path || fmrb_hal_file_stat(path, &info) != FMRB_OK) {
        return FMRB_MP_PATH_NONE;
    }
    return FMRB_S_ISDIR(info.mode) ? FMRB_MP_PATH_DIR : FMRB_MP_PATH_FILE;
}

int fmrb_mp_bridge_file_size(const char *path, uint32_t *out_size) {
    fmrb_file_info_t info;
    if (!path || !out_size || fmrb_hal_file_stat(path, &info) != FMRB_OK) {
        return -1;
    }
    if (FMRB_S_ISDIR(info.mode)) {
        return -1;
    }
    *out_size = (uint32_t)info.size;
    return 0;
}

int fmrb_mp_bridge_file_read(const char *path, uint8_t *buf, uint32_t size) {
    if (!path || (!buf && size > 0)) {
        return -1;
    }

    fmrb_file_t file = NULL;
    if (fmrb_hal_file_open(path, FMRB_O_RDONLY, &file) != FMRB_OK) {
        return -1;
    }

    // One read is not guaranteed to return everything, so keep going until the
    // buffer is full or the file ends short (which is an error here: the caller
    // sized the buffer from stat, and a short file means it changed underneath).
    uint32_t done = 0;
    int ret = 0;
    while (done < size) {
        size_t got = 0;
        if (fmrb_hal_file_read(file, buf + done, size - done, &got) != FMRB_OK || got == 0) {
            ret = -1;
            break;
        }
        done += (uint32_t)got;
    }
    fmrb_hal_file_close(file);
    return ret;
}

bool fmrb_mp_bridge_app_dir(char *out, size_t cap) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!out || cap == 0 || !ctx || ctx->load_mode != FMRB_LOAD_MODE_FILE) {
        return false;
    }

    const char *slash = strrchr(ctx->filepath, '/');
    if (!slash || slash == ctx->filepath) {
        return false;
    }
    size_t len = (size_t)(slash - ctx->filepath);
    if (len >= cap) {
        return false;
    }
    memcpy(out, ctx->filepath, len);
    out[len] = '\0';
    return true;
}

void fmrb_mp_bridge_log(char level, const char *msg) {
    if (!msg) {
        return;
    }
    switch (level) {
        case 'D': FMRB_LOGD(TAG, "%s", msg); break;
        case 'W': FMRB_LOGW(TAG, "%s", msg); break;
        case 'E': FMRB_LOGE(TAG, "%s", msg); break;
        default:  FMRB_LOGI(TAG, "%s", msg); break;
    }
}
