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
#include "fmrb_gfx.h"
#include "fmrb_gfx_cmd.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"
#include "fmrb_rtos.h"

#include "fmrb_mp_bridge.h"

static const char *TAG = "fmrb_mp_mod";

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
                          int bg_color, bool has_bg) {
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_text(&cmd, (fmrb_canvas_handle_t)canvas_id, (int16_t)x, (int16_t)y, text,
                      (fmrb_color_t)color, (fmrb_color_t)bg_color, !has_bg,
                      FMRB_FONT_SIZE_MEDIUM, 0);
    return fmrb_gfx_submit(&cmd) == FMRB_OK ? 0 : -1;
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
