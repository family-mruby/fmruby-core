#include <string.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/string.h>
#include <mruby/variable.h>

#include "fmrb_app.h"
#include "fmrb_hal.h"
#include "fmrb_gfx.h"
#include "fmrb_err.h"
#include "fmrb_log.h"
#include "../../include/picoruby_fmrb_app.h"
#include "app_local.h"

static const char* TAG = "gfx";

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

    FMRB_LOGI(TAG, "clear() called with color=0x%08x", (unsigned int)color);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        FMRB_LOGE(TAG, "clear() failed: Graphics not initialized");
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    // Pass canvas_id directly (thread-safe)
    fmrb_gfx_err_t ret = fmrb_gfx_clear(data->ctx, data->canvas_id, (fmrb_color_t)color);
    if (ret != FMRB_GFX_OK) {
        FMRB_LOGE(TAG, "fmrb_gfx_clear failed: %d", ret);
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Graphics clear failed: %d", ret);
    }

    FMRB_LOGI(TAG, "clear() succeeded");
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

    // Pass canvas_id directly (thread-safe)
    fmrb_gfx_err_t ret = fmrb_gfx_set_pixel(data->ctx, data->canvas_id, (int16_t)x, (int16_t)y, (fmrb_color_t)color);
    if (ret != FMRB_GFX_OK) {
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

    // Pass canvas_id directly (thread-safe)
    fmrb_gfx_err_t ret = fmrb_gfx_draw_line(data->ctx, data->canvas_id, (int16_t)x1, (int16_t)y1,
                                             (int16_t)x2, (int16_t)y2, (fmrb_color_t)color);
    if (ret != FMRB_GFX_OK) {
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

    // Pass canvas_id directly (thread-safe)
    fmrb_rect_t rect = {(int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h};
    fmrb_gfx_err_t ret = fmrb_gfx_draw_rect(data->ctx, data->canvas_id, &rect, (fmrb_color_t)color);
    if (ret != FMRB_GFX_OK) {
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

    // Pass canvas_id directly (thread-safe)
    fmrb_rect_t rect = {(int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h};
    fmrb_gfx_err_t ret = fmrb_gfx_fill_rect(data->ctx, data->canvas_id, &rect, (fmrb_color_t)color);
    if (ret != FMRB_GFX_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Fill rect failed: %d", ret);
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

    // Pass canvas_id directly (thread-safe)
    fmrb_gfx_err_t ret = fmrb_gfx_draw_circle(data->ctx, data->canvas_id, (int32_t)x, (int32_t)y,
                                               (int32_t)r, (fmrb_color_t)color);
    if (ret != FMRB_GFX_OK) {
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

    // Pass canvas_id directly (thread-safe)
    fmrb_gfx_err_t ret = fmrb_gfx_fill_circle(data->ctx, data->canvas_id, (int32_t)x, (int32_t)y,
                                               (int32_t)r, (fmrb_color_t)color);
    if (ret != FMRB_GFX_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Fill circle failed: %d", ret);
    }

    return self;
}

// Graphics#draw_text(x, y, text, color)
static mrb_value mrb_gfx_draw_text(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, color;
    char *text;
    mrb_get_args(mrb, "iizi", &x, &y, &text, &color);

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    // Pass canvas_id directly (thread-safe)
    fmrb_gfx_err_t ret = fmrb_gfx_draw_text(data->ctx, data->canvas_id, (int16_t)x, (int16_t)y,
                                             text, (fmrb_color_t)color, FMRB_FONT_SIZE_MEDIUM);
    if (ret != FMRB_GFX_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Draw text failed: %d", ret);
    }

    return self;
}

// Graphics#present
static mrb_value mrb_gfx_present(mrb_state *mrb, mrb_value self)
{
    FMRB_LOGI(TAG, "present() called");

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) {
        FMRB_LOGE(TAG, "present() failed: Graphics not initialized");
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    // Pass canvas_id directly (thread-safe)
    fmrb_gfx_err_t ret = fmrb_gfx_present(data->ctx, data->canvas_id);
    if (ret != FMRB_GFX_OK) {
        FMRB_LOGE(TAG, "fmrb_gfx_present failed: %d", ret);
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Present failed: %d", ret);
    }

    FMRB_LOGI(TAG, "present() succeeded");
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
    mrb_define_method(mrb, gfx_class, "draw_circle", mrb_gfx_draw_circle, MRB_ARGS_REQ(4));
    mrb_define_method(mrb, gfx_class, "fill_circle", mrb_gfx_fill_circle, MRB_ARGS_REQ(4));
    mrb_define_method(mrb, gfx_class, "draw_text", mrb_gfx_draw_text, MRB_ARGS_REQ(4));
    mrb_define_method(mrb, gfx_class, "present", mrb_gfx_present, MRB_ARGS_NONE());
    mrb_define_method(mrb, gfx_class, "destroy", mrb_gfx_destroy, MRB_ARGS_NONE());

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
