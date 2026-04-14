#include <string.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/string.h>
#include <mruby/variable.h>
#include <mruby/hash.h>
#include <mruby/array.h>

#include "fmrb_app.h"
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_gfx.h"
#include "fmrb_err.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_file_transfer_msg.h"
#include "fmrb_mem.h"
#include "fmrb_task_config.h"
#include "fmrb_link_protocol.h"
#include "fmrb_transport.h"
#include "../../include/picoruby_fmrb_app.h"
#include "app_local.h"
#include "host_task.h"

static const char* TAG = "gfx";

// Helper function to send GFX command message to Host Task
static fmrb_err_t send_gfx_command(const gfx_cmd_t *cmd) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        FMRB_LOGE(TAG, "Failed to get current task context");
        return FMRB_ERR_INVALID_STATE;
    }

    // Acquire semaphore to ensure HOST queue has available space
    // This reserves FMRB_HOST_HID_RESERVED_SLOTS (32) for HID events
    // and limits GFX commands to FMRB_HOST_GFX_AVAILABLE_SLOTS (96)
    fmrb_semaphore_t sem = fmrb_host_get_gfx_queue_semaphore();
    if (sem) {
        // Block until semaphore is available (HOST queue has space)
        // Use infinite timeout - app will naturally wait for queue space
        fmrb_base_type_t sem_ret = fmrb_semaphore_take(sem, UINT32_MAX);
        if (sem_ret != FMRB_PASS) {
            FMRB_LOGE(TAG, "Failed to acquire GFX queue semaphore: %d", sem_ret);
            return FMRB_ERR_TIMEOUT;
        }
    }

    fmrb_msg_t msg = {
        .type = FMRB_MSG_TYPE_APP_GFX,
        .src_pid = ctx->app_id,
        .size = sizeof(gfx_cmd_t)
    };
    memcpy(msg.data, cmd, sizeof(gfx_cmd_t));

    // Send to HOST task (passthrough to Graphics-Audio board)
    fmrb_err_t ret = fmrb_msg_send(PROC_ID_HOST, &msg, 5000);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "Failed to send graphics command: %d", ret);
        // Release semaphore on failure so we don't leak the slot
        if (sem) {
            fmrb_semaphore_give(sem);
        }
    }
    // On success, HOST task will release the semaphore after processing
    return ret;
}

// Graphics context wrapper for mruby
typedef struct {
    fmrb_gfx_context_t ctx;
    fmrb_canvas_handle_t canvas_id;  // Canvas ID for this instance
} mrb_gfx_data;

static void mrb_gfx_data_free(mrb_state *mrb, void *ptr)
{
    if (ptr) {
        // Don't deinitialize global context, just free the wrapper
        // The global context is managed by fmrb_gfx layer
        mrb_free(mrb, ptr);
    }
}

static const struct mrb_data_type mrb_gfx_data_type = {
    "Graphics", mrb_gfx_data_free,
};

// Graphics.new(canvas_id)
static mrb_value mrb_gfx_initialize(mrb_state *mrb, mrb_value self)
{
    mrb_int canvas_id;
    mrb_get_args(mrb, "i", &canvas_id);

    FMRB_LOGI(TAG, "FmrbGfx.new called: canvas_id=%d", (int)canvas_id);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_malloc(mrb, sizeof(mrb_gfx_data));
    memset(data, 0, sizeof(mrb_gfx_data));

    // Get global graphics context (already initialized by kernel/host)
    data->ctx = fmrb_gfx_get_global_context();
    if (!data->ctx) {
        FMRB_LOGE(TAG, "Global graphics context not initialized");
        mrb_free(mrb, data);
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
    }

    // Store canvas_id for this instance
    data->canvas_id = (fmrb_canvas_handle_t)canvas_id;

    FMRB_LOGI(TAG, "FmrbGfx initialized: canvas_id=%d, ctx=%p",
              (int)data->canvas_id, data->ctx);

    mrb_data_init(self, data, &mrb_gfx_data_type);
    return self;
}

// Graphics#clear(color)
static mrb_value mrb_gfx_clear(mrb_state *mrb, mrb_value self)
{
    mrb_int color;
    mrb_get_args(mrb, "i", &color);

    FMRB_LOGD(TAG, "clear() called with color=0x%08x", (unsigned int)color);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        FMRB_LOGE(TAG, "clear() failed: Graphics not initialized");
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    // Send GFX command to Host Task
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_CLEAR,
        .canvas_id = data->canvas_id,
        .params.clear.color = (fmrb_color_t)color
    };

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "clear() failed: %d", ret);
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Graphics clear failed: %d", ret);
    }

    FMRB_LOGD(TAG, "clear() succeeded");
    return self;
}

// Graphics#set_pixel(x, y, color)
static mrb_value mrb_gfx_set_pixel(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, color;
    mrb_get_args(mrb, "iii", &x, &y, &color);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    // Send GFX command to Host Task
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_PIXEL,
        .canvas_id = data->canvas_id,
        .params.pixel = {.x = (int16_t)x, .y = (int16_t)y, .color = (fmrb_color_t)color}
    };

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Set pixel failed: %d", ret);
    }

    return self;
}

// Graphics#draw_line(x1, y1, x2, y2, color)
static mrb_value mrb_gfx_draw_line(mrb_state *mrb, mrb_value self)
{
    mrb_int x1, y1, x2, y2, color;
    mrb_get_args(mrb, "iiiii", &x1, &y1, &x2, &y2, &color);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    // Send GFX command to Host Task
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_LINE,
        .canvas_id = data->canvas_id,
        .params.line = {
            .x1 = (int16_t)x1, .y1 = (int16_t)y1,
            .x2 = (int16_t)x2, .y2 = (int16_t)y2,
            .color = (fmrb_color_t)color
        }
    };

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Draw line failed: %d", ret);
    }

    return self;
}

// Graphics#draw_rect(x, y, w, h, color)
static mrb_value mrb_gfx_draw_rect(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, w, h, color;
    mrb_get_args(mrb, "iiiii", &x, &y, &w, &h, &color);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    // Send GFX command to Host Task
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_RECT,
        .canvas_id = data->canvas_id,
        .params.rect = {
            .rect = {(int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h},
            .color = (fmrb_color_t)color,
            .filled = false
        }
    };

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Draw rect failed: %d", ret);
    }

    return self;
}

// Graphics#fill_rect(x, y, w, h, color)
static mrb_value mrb_gfx_fill_rect(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, w, h, color;
    mrb_get_args(mrb, "iiiii", &x, &y, &w, &h, &color);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    FMRB_LOGD("gfx", "fill_rect called: x=%d, y=%d, w=%d, h=%d, color=0x%02X, canvas_id=%d",
              (int)x, (int)y, (int)w, (int)h, (int)color, data->canvas_id);

    // Send GFX command to Host Task
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_RECT,
        .canvas_id = data->canvas_id,
        .params.rect = {
            .rect = {(int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h},
            .color = (fmrb_color_t)color,
            .filled = true
        }
    };

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        FMRB_LOGE("gfx", "fill_rect send_gfx_command failed: %d", ret);
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Fill rect failed: %d", ret);
    }

    FMRB_LOGD("gfx", "fill_rect command sent successfully");
    return self;
}

// Graphics#blend_rect(x, y, w, h, color, mode)
// Blend color into each pixel in the rectangle
// mode: 0=ADD (saturating), 1=XOR
static mrb_value mrb_gfx_blend_rect(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, w, h, color, mode;
    mrb_get_args(mrb, "iiiiii", &x, &y, &w, &h, &color, &mode);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_BLEND_RECT,
        .canvas_id = data->canvas_id,
        .params.blend_rect = {
            .rect = {(int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h},
            .color = (fmrb_color_t)color,
            .mode = (uint8_t)mode
        }
    };

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "blend_rect failed: %d", ret);
    }

    return self;
}

// Graphics#draw_circle(x, y, r, color)
static mrb_value mrb_gfx_draw_circle(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, r, color;
    mrb_get_args(mrb, "iiii", &x, &y, &r, &color);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    // Send GFX command to Host Task
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_CIRCLE,
        .canvas_id = data->canvas_id,
        .params.circle = {
            .x = (int16_t)x,
            .y = (int16_t)y,
            .radius = (int16_t)r,
            .color = (fmrb_color_t)color,
            .filled = false
        }
    };

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Draw circle failed: %d", ret);
    }

    return self;
}

// Graphics#fill_circle(x, y, r, color)
static mrb_value mrb_gfx_fill_circle(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, r, color;
    mrb_get_args(mrb, "iiii", &x, &y, &r, &color);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    FMRB_LOGD("gfx", "fill_circle called: x=%d, y=%d, r=%d, color=0x%02X, canvas_id=%d",
              (int)x, (int)y, (int)r, (int)color, data->canvas_id);

    // Send GFX command to Host Task
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_CIRCLE,
        .canvas_id = data->canvas_id,
        .params.circle = {
            .x = (int16_t)x,
            .y = (int16_t)y,
            .radius = (int16_t)r,
            .color = (fmrb_color_t)color,
            .filled = true
        }
    };

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        FMRB_LOGE("gfx", "fill_circle send_gfx_command failed: %d", ret);
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Fill circle failed: %d", ret);
    }

    return self;
}

// Graphics#draw_round_rect(x, y, w, h, r, color)
static mrb_value mrb_gfx_draw_round_rect(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, w, h, r, color;
    mrb_get_args(mrb, "iiiiii", &x, &y, &w, &h, &r, &color);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    gfx_cmd_t cmd = { .cmd_type = GFX_CMD_ROUND_RECT, .canvas_id = data->canvas_id,
        .params.round_rect = { .x=(int16_t)x, .y=(int16_t)y, .w=(int16_t)w, .h=(int16_t)h,
                               .radius=(int16_t)r, .color=(fmrb_color_t)color, .filled=false }};
    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) mrb_raisef(mrb, E_RUNTIME_ERROR, "Draw round rect failed: %d", ret);
    return self;
}

// Graphics#fill_round_rect(x, y, w, h, r, color)
static mrb_value mrb_gfx_fill_round_rect(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, w, h, r, color;
    mrb_get_args(mrb, "iiiiii", &x, &y, &w, &h, &r, &color);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    gfx_cmd_t cmd = { .cmd_type = GFX_CMD_ROUND_RECT, .canvas_id = data->canvas_id,
        .params.round_rect = { .x=(int16_t)x, .y=(int16_t)y, .w=(int16_t)w, .h=(int16_t)h,
                               .radius=(int16_t)r, .color=(fmrb_color_t)color, .filled=true }};
    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) mrb_raisef(mrb, E_RUNTIME_ERROR, "Fill round rect failed: %d", ret);
    return self;
}

// Graphics#draw_ellipse(x, y, rx, ry, color)
static mrb_value mrb_gfx_draw_ellipse(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, rx, ry, color;
    mrb_get_args(mrb, "iiiii", &x, &y, &rx, &ry, &color);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    gfx_cmd_t cmd = { .cmd_type = GFX_CMD_ELLIPSE, .canvas_id = data->canvas_id,
        .params.ellipse = { .x=(int16_t)x, .y=(int16_t)y, .rx=(int16_t)rx, .ry=(int16_t)ry,
                            .color=(fmrb_color_t)color, .filled=false }};
    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) mrb_raisef(mrb, E_RUNTIME_ERROR, "Draw ellipse failed: %d", ret);
    return self;
}

// Graphics#fill_ellipse(x, y, rx, ry, color)
static mrb_value mrb_gfx_fill_ellipse(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, rx, ry, color;
    mrb_get_args(mrb, "iiiii", &x, &y, &rx, &ry, &color);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    gfx_cmd_t cmd = { .cmd_type = GFX_CMD_ELLIPSE, .canvas_id = data->canvas_id,
        .params.ellipse = { .x=(int16_t)x, .y=(int16_t)y, .rx=(int16_t)rx, .ry=(int16_t)ry,
                            .color=(fmrb_color_t)color, .filled=true }};
    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) mrb_raisef(mrb, E_RUNTIME_ERROR, "Fill ellipse failed: %d", ret);
    return self;
}

// Graphics#draw_triangle(x0, y0, x1, y1, x2, y2, color)
static mrb_value mrb_gfx_draw_triangle(mrb_state *mrb, mrb_value self)
{
    mrb_int x0, y0, x1, y1, x2, y2, color;
    mrb_get_args(mrb, "iiiiiii", &x0, &y0, &x1, &y1, &x2, &y2, &color);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    gfx_cmd_t cmd = { .cmd_type = GFX_CMD_TRIANGLE, .canvas_id = data->canvas_id,
        .params.triangle = { .x0=(int16_t)x0, .y0=(int16_t)y0, .x1=(int16_t)x1, .y1=(int16_t)y1,
                             .x2=(int16_t)x2, .y2=(int16_t)y2, .color=(fmrb_color_t)color, .filled=false }};
    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) mrb_raisef(mrb, E_RUNTIME_ERROR, "Draw triangle failed: %d", ret);
    return self;
}

// Graphics#fill_triangle(x0, y0, x1, y1, x2, y2, color)
static mrb_value mrb_gfx_fill_triangle(mrb_state *mrb, mrb_value self)
{
    mrb_int x0, y0, x1, y1, x2, y2, color;
    mrb_get_args(mrb, "iiiiiii", &x0, &y0, &x1, &y1, &x2, &y2, &color);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    gfx_cmd_t cmd = { .cmd_type = GFX_CMD_TRIANGLE, .canvas_id = data->canvas_id,
        .params.triangle = { .x0=(int16_t)x0, .y0=(int16_t)y0, .x1=(int16_t)x1, .y1=(int16_t)y1,
                             .x2=(int16_t)x2, .y2=(int16_t)y2, .color=(fmrb_color_t)color, .filled=true }};
    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) mrb_raisef(mrb, E_RUNTIME_ERROR, "Fill triangle failed: %d", ret);
    return self;
}

// Graphics#draw_arc(x, y, r0, r1, angle0, angle1, color)
static mrb_value mrb_gfx_draw_arc(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, r0, r1, angle0, angle1, color;
    mrb_get_args(mrb, "iiiiiii", &x, &y, &r0, &r1, &angle0, &angle1, &color);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    gfx_cmd_t cmd = { .cmd_type = GFX_CMD_ARC, .canvas_id = data->canvas_id,
        .params.arc = { .x=(int16_t)x, .y=(int16_t)y, .r0=(int16_t)r0, .r1=(int16_t)r1,
                        .angle0=(int16_t)angle0, .angle1=(int16_t)angle1,
                        .color=(fmrb_color_t)color, .filled=false }};
    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) mrb_raisef(mrb, E_RUNTIME_ERROR, "Draw arc failed: %d", ret);
    return self;
}

// Graphics#fill_arc(x, y, r0, r1, angle0, angle1, color)
static mrb_value mrb_gfx_fill_arc(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, r0, r1, angle0, angle1, color;
    mrb_get_args(mrb, "iiiiiii", &x, &y, &r0, &r1, &angle0, &angle1, &color);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    gfx_cmd_t cmd = { .cmd_type = GFX_CMD_ARC, .canvas_id = data->canvas_id,
        .params.arc = { .x=(int16_t)x, .y=(int16_t)y, .r0=(int16_t)r0, .r1=(int16_t)r1,
                        .angle0=(int16_t)angle0, .angle1=(int16_t)angle1,
                        .color=(fmrb_color_t)color, .filled=true }};
    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) mrb_raisef(mrb, E_RUNTIME_ERROR, "Fill arc failed: %d", ret);
    return self;
}

// Graphics#set_text_size(size)
static mrb_value mrb_gfx_set_text_size(mrb_state *mrb, mrb_value self)
{
    mrb_int size;
    mrb_get_args(mrb, "i", &size);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    if (size < 1) size = 1;
    if (size > 4) size = 4;
    gfx_cmd_t cmd = { .cmd_type = GFX_CMD_TEXT_SIZE, .canvas_id = data->canvas_id,
        .params.text_size = { .size = (uint8_t)size }};
    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) mrb_raisef(mrb, E_RUNTIME_ERROR, "Set text size failed: %d", ret);
    return self;
}

// Graphics#draw_text(x, y, text, color [, bg_color])
static mrb_value mrb_gfx_draw_text(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, color;
    mrb_int bg_color = 0;
    mrb_bool bg_given = FALSE;
    char *text;

    mrb_int argc = mrb_get_argc(mrb);
    if (argc == 5) {
        mrb_get_args(mrb, "iizii", &x, &y, &text, &color, &bg_color);
        bg_given = TRUE;
    } else {
        mrb_get_args(mrb, "iizi", &x, &y, &text, &color);
    }

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    FMRB_LOGD("gfx", "draw_text called: x=%d, y=%d, text='%s', color=0x%02X, bg_color=0x%02X, bg_transparent=%d, canvas_id=%d",
              (int)x, (int)y, text, (int)color, (int)bg_color, !bg_given, data->canvas_id);

    // Send GFX command to Host Task
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_TEXT,
        .canvas_id = data->canvas_id,
        .params.text = {
            .x = (int16_t)x,
            .y = (int16_t)y,
            .color = (fmrb_color_t)color,
            .bg_color = (fmrb_color_t)bg_color,
            .bg_transparent = !bg_given,
            .font_size = FMRB_FONT_SIZE_MEDIUM
        }
    };
    strncpy(cmd.params.text.text, text, sizeof(cmd.params.text.text) - 1);
    cmd.params.text.text[sizeof(cmd.params.text.text) - 1] = '\0';

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        FMRB_LOGE("gfx", "draw_text send_gfx_command failed: %d", ret);
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Draw text failed: %d", ret);
    }

    return self;
}

// Graphics#present
static mrb_value mrb_gfx_present(mrb_state *mrb, mrb_value self)
{
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        FMRB_LOGE(TAG, "present() failed: Graphics not initialized");
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    // Get window position from app context
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        FMRB_LOGE(TAG, "present() failed: No app context");
        mrb_raise(mrb, E_RUNTIME_ERROR, "No app context");
    }

    // Send PRESENT command to Host Task with window position
    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_PRESENT,
        .canvas_id = data->canvas_id,
        .params.present = {
            .x = (int16_t)ctx->window_pos_x,
            .y = (int16_t)ctx->window_pos_y,
            .transparent_color = 0xFF  // No transparency by default
        }
    };

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "present() failed: %d", ret);
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Present failed: %d", ret);
    }

    return self;
}

// Graphics#destroy - Explicitly release graphics resources
static mrb_value mrb_gfx_destroy(mrb_state *mrb, mrb_value self)
{
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (data && data->ctx) {
        data->ctx = NULL;
    }
    return mrb_nil_value();
}

// ============================================================
// File Transfer API
// ============================================================

// Helper: send file_cmd_t to host_task and wait for result
static fmrb_err_t send_file_cmd_sync(file_cmd_t *cmd,
                                     file_cmd_result_t *result,
                                     uint32_t timeout_ms)
{
    result->done_sem = fmrb_semaphore_create_binary();
    if (!result->done_sem) {
        return FMRB_ERR_NO_MEMORY;
    }
    result->result = -99;  // Sentinel

    // Point cmd to the caller's result structure
    cmd->result = result;

    fmrb_msg_t msg = {
        .type = FMRB_MSG_TYPE_FILE_TRANSFER,
        .src_pid = 0,
        .size = sizeof(file_cmd_t)
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

    // Wait for host_task to complete the operation
    fmrb_base_type_t sem_ret = fmrb_semaphore_take(result->done_sem,
        FMRB_MS_TO_TICKS(timeout_ms));
    fmrb_semaphore_delete(result->done_sem);

    if (sem_ret != FMRB_PASS) {
        return FMRB_ERR_TIMEOUT;
    }

    return FMRB_OK;
}

// FmrbGfx#_transfer_file(src_path, dest_path) -> true/false
// src_path: local file to read (HAL resolves flash prefix)
// dest_path: destination path on graphics-audio LittleFS
static mrb_value mrb_gfx_transfer_file(mrb_state *mrb, mrb_value self)
{
    char *path;
    char *dest;
    mrb_get_args(mrb, "zz", &path, &dest);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    // Validate dest path length (must fit in file_cmd_t.path[120])
    size_t dest_len = strlen(dest);
    if (dest_len >= 120) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Dest path too long");
    }

    // HAL adds base path ("/flash" on ESP32, "flash" on POSIX) automatically
    fmrb_file_info_t info;
    fmrb_err_t ret = fmrb_hal_file_stat(path, &info);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "transfer_file: file not found: %s", path);
        mrb_raisef(mrb, E_RUNTIME_ERROR, "File not found: %s", path);
    }

    uint32_t file_size = (uint32_t)info.size;
    if (file_size == 0) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "File is empty");
    }

    // Allocate buffer and read file
    uint8_t *file_data = (uint8_t *)fmrb_sys_malloc(file_size);
    if (!file_data) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to allocate %d bytes", file_size);
    }

    fmrb_file_t fh;
    ret = fmrb_hal_file_open(path, FMRB_O_RDONLY, &fh);
    if (ret != FMRB_OK) {
        fmrb_sys_free(file_data);
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to open file: %s", path);
    }

    size_t bytes_read = 0;
    ret = fmrb_hal_file_read(fh, file_data, file_size, &bytes_read);
    fmrb_hal_file_close(fh);

    if (ret != FMRB_OK || bytes_read != file_size) {
        fmrb_sys_free(file_data);
        mrb_raise(mrb, E_RUNTIME_ERROR, "Failed to read file");
    }

    // Send transfer command to host_task
    // Note: file_data ownership is transferred to host_task
    // Use dest path as the target location on graphics-audio LittleFS
    file_cmd_t cmd = {0};
    file_cmd_result_t result = {0};
    cmd.cmd_type = FILE_CMD_TRANSFER;
    cmd.path_len = (uint16_t)dest_len;
    memcpy(cmd.path, dest, dest_len);
    cmd.params.transfer.data = file_data;
    cmd.params.transfer.data_len = file_size;

    ret = send_file_cmd_sync(&cmd, &result, 30000);  // 30s timeout for large files
    if (ret != FMRB_OK) {
        // If send failed, we still own the buffer
        fmrb_sys_free(file_data);
        mrb_raise(mrb, E_RUNTIME_ERROR, "File transfer timeout");
    }

    // Check result (set by host_task via result pointer)
    if (result.result != 0) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "File transfer failed: %d", result.result);
    }

    FMRB_LOGI(TAG, "transfer_file: %s -> %s (%u bytes) transferred", path, dest, (unsigned)file_size);
    return mrb_true_value();
}

// FmrbGfx#_file_status(path) -> Hash {exists: bool, size: int}
// NOTE: Calls transport_send_sync directly (not via host_task) to avoid deadlock.
// host_task runs transport_process() in its loop, so send_sync from host_task
// would block the process loop and never receive the ACK.
static mrb_value mrb_gfx_file_status(mrb_state *mrb, mrb_value self)
{
    char *path;
    mrb_get_args(mrb, "z", &path);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    size_t path_len = strlen(path);
    if (path_len >= 120) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Path too long");
    }

    // Build payload: header + path
    uint8_t payload_buf[sizeof(fmrb_link_file_transfer_status_t) + 120];
    size_t payload_len = sizeof(fmrb_link_file_transfer_status_t) + path_len;

    fmrb_link_file_transfer_status_t *hdr = (fmrb_link_file_transfer_status_t *)payload_buf;
    hdr->path_len = (uint16_t)path_len;
    memcpy(payload_buf + sizeof(fmrb_link_file_transfer_status_t), path, path_len);

    // Send directly via transport (bypasses host_task to avoid deadlock)
    uint8_t resp_buf[sizeof(fmrb_link_file_transfer_status_resp_t)];
    uint32_t resp_len = sizeof(resp_buf);

    fmrb_err_t ret = fmrb_transport_send_sync(
        FMRB_LINK_TYPE_FILE_TRANSFER,
        FMRB_LINK_FILE_TRANSFER_STATUS,
        payload_buf, payload_len,
        resp_buf, &resp_len,
        5000);

    mrb_value hash = mrb_hash_new(mrb);
    if (ret == FMRB_OK && resp_len >= sizeof(fmrb_link_file_transfer_status_resp_t)) {
        fmrb_link_file_transfer_status_resp_t *resp =
            (fmrb_link_file_transfer_status_resp_t *)resp_buf;
        mrb_hash_set(mrb, hash,
            mrb_symbol_value(mrb_intern_lit(mrb, "exists")),
            resp->exists ? mrb_true_value() : mrb_false_value());
        mrb_hash_set(mrb, hash,
            mrb_symbol_value(mrb_intern_lit(mrb, "size")),
            mrb_fixnum_value(resp->file_size));
    } else {
        mrb_hash_set(mrb, hash,
            mrb_symbol_value(mrb_intern_lit(mrb, "exists")),
            mrb_false_value());
        mrb_hash_set(mrb, hash,
            mrb_symbol_value(mrb_intern_lit(mrb, "size")),
            mrb_fixnum_value(0));
    }

    return hash;
}

// ============================================================
// Image API
// ============================================================

// FmrbGfx#_draw_image(image_id, x, y)
static mrb_value mrb_gfx_draw_image(mrb_state *mrb, mrb_value self)
{
    mrb_int image_id, x, y;
    mrb_float scale_x, scale_y;
    mrb_get_args(mrb, "iiiff", &image_id, &x, &y, &scale_x, &scale_y);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_DRAW_IMAGE,
        .canvas_id = data->canvas_id,
        .params.draw_image = {
            .image_id = (uint16_t)image_id,
            .x = (int16_t)x,
            .y = (int16_t)y,
            .flags = 0,
            .scale_x_fp8 = (int16_t)(scale_x * 256.0f),
            .scale_y_fp8 = (int16_t)(scale_y * 256.0f)
        }
    };

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "draw_image failed: %d", ret);
    }

    return self;
}

// FmrbGfx#_delete_image(image_id)
static mrb_value mrb_gfx_delete_image(mrb_state *mrb, mrb_value self)
{
    mrb_int image_id;
    mrb_get_args(mrb, "i", &image_id);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    gfx_cmd_t cmd = {
        .cmd_type = GFX_CMD_DELETE_IMAGE,
        .canvas_id = data->canvas_id,
        .params.delete_image = {
            .image_id = (uint16_t)image_id
        }
    };

    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "delete_image failed: %d", ret);
    }

    return self;
}

// FmrbGfx#_create_image_from_file(path) -> image_id
// Sends CREATE_IMAGE_FROM_FILE directly via transport (bypasses host_task to avoid deadlock)
static mrb_value mrb_gfx_create_image_from_file(mrb_state *mrb, mrb_value self)
{
    char *path;
    mrb_get_args(mrb, "z", &path);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    size_t path_len = strlen(path);
    if (path_len >= 120) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Path too long");
    }

    // Build payload: header + path
    uint8_t payload_buf[sizeof(fmrb_link_graphics_create_image_from_file_t) + 120];
    size_t payload_len = sizeof(fmrb_link_graphics_create_image_from_file_t) + path_len;

    fmrb_link_graphics_create_image_from_file_t *hdr =
        (fmrb_link_graphics_create_image_from_file_t *)payload_buf;
    hdr->canvas_id = data->canvas_id;
    hdr->path_len = (uint16_t)path_len;
    memcpy(payload_buf + sizeof(fmrb_link_graphics_create_image_from_file_t), path, path_len);

    // Send sync to get image_id back
    uint8_t resp_buf[sizeof(fmrb_link_graphics_image_created_t)];
    uint32_t resp_len = sizeof(resp_buf);

    fmrb_err_t ret = fmrb_transport_send_sync(
        FMRB_LINK_TYPE_GRAPHICS,
        FMRB_LINK_GFX_CREATE_IMAGE_FROM_FILE,
        payload_buf, payload_len,
        resp_buf, &resp_len,
        10000);  // 10s timeout for PNG decode

    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "create_image_from_file failed: %d", ret);
        mrb_raisef(mrb, E_RUNTIME_ERROR, "create_image_from_file failed: %d", ret);
    }

    if (resp_len >= sizeof(fmrb_link_graphics_image_created_t)) {
        fmrb_link_graphics_image_created_t *resp =
            (fmrb_link_graphics_image_created_t *)resp_buf;
        FMRB_LOGI(TAG, "create_image_from_file: id=%u, %ux%u",
                  resp->image_id, resp->width, resp->height);
        // Return Hash {id: image_id, width: w, height: h}
        mrb_value hash = mrb_hash_new(mrb);
        mrb_hash_set(mrb, hash,
            mrb_symbol_value(mrb_intern_lit(mrb, "id")),
            mrb_fixnum_value(resp->image_id));
        mrb_hash_set(mrb, hash,
            mrb_symbol_value(mrb_intern_lit(mrb, "width")),
            mrb_fixnum_value(resp->width));
        mrb_hash_set(mrb, hash,
            mrb_symbol_value(mrb_intern_lit(mrb, "height")),
            mrb_fixnum_value(resp->height));
        return hash;
    }

    FMRB_LOGW(TAG, "create_image_from_file: no response data");
    return mrb_nil_value();
}

// FmrbGfx.hsv_to_rgb(h, s, v) -> [r, g, b]
// h: 0-360, s: 0-255, v: 0-255
static mrb_value mrb_gfx_s_hsv_to_rgb(mrb_state *mrb, mrb_value klass)
{
    mrb_int h, s, v;
    mrb_get_args(mrb, "iii", &h, &s, &v);

    if (h < 0) h = 0;
    if (h >= 360) h = h % 360;
    if (s < 0) s = 0;
    if (s > 255) s = 255;
    if (v < 0) v = 0;
    if (v > 255) v = 255;

    int r, g, b;
    if (s == 0) {
        r = g = b = v;
    } else {
        int region = h / 60;
        int remainder = (h - region * 60) * 255 / 60;
        int p = v * (255 - s) / 255;
        int q = v * (255 - s * remainder / 255) / 255;
        int t = v * (255 - s * (255 - remainder) / 255) / 255;

        switch (region) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
        }
    }

    mrb_value ary = mrb_ary_new_capa(mrb, 3);
    mrb_ary_push(mrb, ary, mrb_fixnum_value(r));
    mrb_ary_push(mrb, ary, mrb_fixnum_value(g));
    mrb_ary_push(mrb, ary, mrb_fixnum_value(b));
    return ary;
}

// FmrbGfx.rgb_to_332(r, g, b) -> Integer
// r,g,b: 0-255
static mrb_value mrb_gfx_s_rgb_to_332(mrb_state *mrb, mrb_value klass)
{
    mrb_int r, g, b;
    mrb_get_args(mrb, "iii", &r, &g, &b);

    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;

    return mrb_fixnum_value(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6));
}

static mrb_value mrb_gfx_set_output_level(mrb_state *mrb, mrb_value self)
{
    mrb_int level;
    mrb_get_args(mrb, "i", &level);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    if (level < 0) level = 0;
    if (level > 255) level = 255;
    gfx_cmd_t cmd = { .cmd_type = GFX_CMD_SET_OUTPUT_LEVEL, .canvas_id = data->canvas_id,
        .params.set_output_level = { .level = (uint8_t)level }};
    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) mrb_raisef(mrb, E_RUNTIME_ERROR, "Set output level failed: %d", ret);
    return self;
}

static mrb_value mrb_gfx_set_chroma_level(mrb_state *mrb, mrb_value self)
{
    mrb_int level;
    mrb_get_args(mrb, "i", &level);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    if (level < 0) level = 0;
    if (level > 255) level = 255;
    gfx_cmd_t cmd = { .cmd_type = GFX_CMD_SET_CHROMA_LEVEL, .canvas_id = data->canvas_id,
        .params.set_chroma_level = { .level = (uint8_t)level }};
    fmrb_err_t ret = send_gfx_command(&cmd);
    if (ret != FMRB_OK) mrb_raisef(mrb, E_RUNTIME_ERROR, "Set chroma level failed: %d", ret);
    return self;
}

void mrb_fmrb_gfx_init(mrb_state *mrb)
{
    struct RClass *gfx_class = mrb_define_class(mrb, "FmrbGfx", mrb->object_class);
    MRB_SET_INSTANCE_TT(gfx_class, MRB_TT_DATA);

    mrb_define_method(mrb, gfx_class, "_init", mrb_gfx_initialize, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, gfx_class, "clear", mrb_gfx_clear, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, gfx_class, "set_pixel", mrb_gfx_set_pixel, MRB_ARGS_REQ(3));
    mrb_define_method(mrb, gfx_class, "draw_line", mrb_gfx_draw_line, MRB_ARGS_REQ(5));
    mrb_define_method(mrb, gfx_class, "draw_rect", mrb_gfx_draw_rect, MRB_ARGS_REQ(5));
    mrb_define_method(mrb, gfx_class, "fill_rect", mrb_gfx_fill_rect, MRB_ARGS_REQ(5));
    mrb_define_method(mrb, gfx_class, "blend_rect", mrb_gfx_blend_rect, MRB_ARGS_REQ(6));
    mrb_define_method(mrb, gfx_class, "draw_circle", mrb_gfx_draw_circle, MRB_ARGS_REQ(4));
    mrb_define_method(mrb, gfx_class, "fill_circle", mrb_gfx_fill_circle, MRB_ARGS_REQ(4));
    mrb_define_method(mrb, gfx_class, "draw_round_rect", mrb_gfx_draw_round_rect, MRB_ARGS_REQ(6));
    mrb_define_method(mrb, gfx_class, "fill_round_rect", mrb_gfx_fill_round_rect, MRB_ARGS_REQ(6));
    mrb_define_method(mrb, gfx_class, "draw_ellipse", mrb_gfx_draw_ellipse, MRB_ARGS_REQ(5));
    mrb_define_method(mrb, gfx_class, "fill_ellipse", mrb_gfx_fill_ellipse, MRB_ARGS_REQ(5));
    mrb_define_method(mrb, gfx_class, "draw_triangle", mrb_gfx_draw_triangle, MRB_ARGS_REQ(7));
    mrb_define_method(mrb, gfx_class, "fill_triangle", mrb_gfx_fill_triangle, MRB_ARGS_REQ(7));
    mrb_define_method(mrb, gfx_class, "draw_arc", mrb_gfx_draw_arc, MRB_ARGS_REQ(7));
    mrb_define_method(mrb, gfx_class, "fill_arc", mrb_gfx_fill_arc, MRB_ARGS_REQ(7));
    mrb_define_method(mrb, gfx_class, "set_text_size", mrb_gfx_set_text_size, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, gfx_class, "draw_text", mrb_gfx_draw_text, MRB_ARGS_ARG(4, 1));
    mrb_define_method(mrb, gfx_class, "present", mrb_gfx_present, MRB_ARGS_NONE());
    mrb_define_method(mrb, gfx_class, "destroy", mrb_gfx_destroy, MRB_ARGS_NONE());

    // File transfer API
    mrb_define_method(mrb, gfx_class, "_transfer_file", mrb_gfx_transfer_file, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, gfx_class, "_file_status", mrb_gfx_file_status, MRB_ARGS_REQ(1));

    // Image API
    mrb_define_method(mrb, gfx_class, "_create_image_from_file", mrb_gfx_create_image_from_file, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, gfx_class, "_draw_image", mrb_gfx_draw_image, MRB_ARGS_REQ(3));
    mrb_define_method(mrb, gfx_class, "_delete_image", mrb_gfx_delete_image, MRB_ARGS_REQ(1));

    // CVBS/NTSC output control
    mrb_define_method(mrb, gfx_class, "set_output_level", mrb_gfx_set_output_level, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, gfx_class, "set_chroma_level", mrb_gfx_set_chroma_level, MRB_ARGS_REQ(1));

    // Color utility class methods
    mrb_define_class_method(mrb, gfx_class, "hsv_to_rgb", mrb_gfx_s_hsv_to_rgb, MRB_ARGS_REQ(3));
    mrb_define_class_method(mrb, gfx_class, "rgb_to_332", mrb_gfx_s_rgb_to_332, MRB_ARGS_REQ(3));

    // Color constants
    mrb_define_const(mrb, gfx_class, "BLACK", mrb_fixnum_value(FMRB_COLOR_BLACK));
    mrb_define_const(mrb, gfx_class, "WHITE", mrb_fixnum_value(FMRB_COLOR_WHITE));
    mrb_define_const(mrb, gfx_class, "RED", mrb_fixnum_value(FMRB_COLOR_RED));
    mrb_define_const(mrb, gfx_class, "GREEN", mrb_fixnum_value(FMRB_COLOR_GREEN));
    mrb_define_const(mrb, gfx_class, "BLUE", mrb_fixnum_value(FMRB_COLOR_BLUE));
    mrb_define_const(mrb, gfx_class, "YELLOW", mrb_fixnum_value(FMRB_COLOR_YELLOW));
    mrb_define_const(mrb, gfx_class, "CYAN", mrb_fixnum_value(FMRB_COLOR_CYAN));
    mrb_define_const(mrb, gfx_class, "MAGENTA", mrb_fixnum_value(FMRB_COLOR_MAGENTA));
    mrb_define_const(mrb, gfx_class, "GRAY", mrb_fixnum_value(FMRB_COLOR_GRAY));
}

void mrb_fmrb_gfx_final(mrb_state *mrb)
{
    // Cleanup is handled by mrb_gfx_data_free
}
