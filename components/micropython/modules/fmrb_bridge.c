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
#include "fmrb_theme.h"

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
    out->resizable = ctx->resizable;
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

/* The nine theme colours, in the order of fmrb_theme_t. Copied out rather
   than handing the struct over, so the module side keeps needing no fmrb_*
   header (see the note at the top of fmrb_mp_bridge.h). */
void fmrb_mp_bridge_theme(uint8_t out[9]) {
    const fmrb_theme_t *t = fmrb_theme_get();
    out[0] = t->desktop_bg;
    out[1] = t->menu_bg;
    out[2] = t->window_bg;
    out[3] = t->text;
    out[4] = t->text_light;
    out[5] = t->highlight;
    out[6] = t->border;
    out[7] = t->button;
    out[8] = t->dir_color;
}

bool fmrb_mp_bridge_is_file_app(void) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    return ctx && ctx->load_mode == FMRB_LOAD_MODE_FILE && ctx->filepath[0] != '\0';
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

