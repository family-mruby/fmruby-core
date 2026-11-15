#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/string.h>
#include <mruby/variable.h>
#include <mruby/hash.h>
#include <mruby/array.h>

#include "esp_heap_caps.h"
#include "fmrb_app.h"
#include "fmrb_hal.h"
#include "fmrb_rtos.h"
#include "fmrb_gfx.h"
#include "fmrb_gfx_cmd.h"
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
#include "fmrb_kernel.h"
#include "fmrb_app_canvas.h"
#include "fmrb_gfx_drawer.h"
#ifdef FMRB_HAVE_CHECKPOINT
#include <mruby/checkpoint.h>
#endif

static const char* TAG = "gfx";

// Graphics context wrapper for mruby. What it draws on is a drawer of the app
// (fmrb_gfx_drawer.h, doc/app_boundary W4b): the canvas, the drawing origin
// (the top-left of the area the app owns on it, doc/codebase_tidy T4, which
// FmrbApp lays out with _set_origin) and whether the target is a sprite image
// now. They are kept there, below the app boundary, for every engine alike;
// this object keeps the drawer's number.
typedef struct {
    fmrb_gfx_context_t ctx;
    int32_t drawer;                  // the app's drawer, or -1
    // The app the drawer is of: a drawer number means something only in its
    // own app's table, so it is closed only from there.
    const fmrb_app_task_context_t *owner;
#ifdef FMRB_HAVE_CHECKPOINT
    // An FmrbGfx restored from a checkpoint image (doc/checkpoint F3) draws on
    // nothing until _checkpoint_rebind binds it to the canvas of the same role
    // in this program: which one it was, and how an extra canvas was made.
    uint8_t ckpt_role;               // GFX_CKPT_ROLE_*; 0 for one made here
    uint8_t ckpt_transparent;
    uint8_t ckpt_key;
    int16_t ckpt_z_offset;
    uint16_t ckpt_width;
    uint16_t ckpt_height;
#endif
} mrb_gfx_data;

// The canvas a drawer draws on and what a coordinate of the area becomes on
// it (the origin, or nothing into a sprite image). These are for the methods
// not on the boundary yet (video, sprites), which take the canvas's own.
typedef struct {
    fmrb_canvas_handle_t canvas;
    int16_t ox, oy;   // what a drawn coordinate gets
    int16_t ax, ay;   // the origin itself (sprite coordinates)
} gfx_at_t;

static __attribute__((unused)) bool gfx_at(const mrb_gfx_data *data, gfx_at_t *at)
{
    const fmrb_gfx_drawer_t *d = data ? fmrb_gfx_drawer_get(data->drawer) : NULL;
    if (!d) {
        return false;
    }
    at->canvas = (fmrb_canvas_handle_t)d->canvas;
    at->ax = d->ox;
    at->ay = d->oy;
    at->ox = d->to_image ? 0 : d->ox;
    at->oy = d->to_image ? 0 : d->oy;
    return true;
}

// The canvas of a bound object (data->ctx set means its drawer is open).
static fmrb_canvas_handle_t gfx_canvas(const mrb_gfx_data *data)
{
    const fmrb_gfx_drawer_t *d = fmrb_gfx_drawer_get(data->drawer);
    return d ? (fmrb_canvas_handle_t)d->canvas : FMRB_CANVAS_SCREEN;
}

// A sprite coordinate on the canvas: from the origin, also while the drawer
// draws into a sprite image (a sprite instance is placed on the canvas).
static int16_t gfx_sprite_x(const mrb_gfx_data *data, mrb_int v)
{
    const fmrb_gfx_drawer_t *d = fmrb_gfx_drawer_get(data->drawer);
    return (int16_t)(v + (d ? d->ox : 0));
}

static int16_t gfx_sprite_y(const mrb_gfx_data *data, mrb_int v)
{
    const fmrb_gfx_drawer_t *d = fmrb_gfx_drawer_get(data->drawer);
    return (int16_t)(v + (d ? d->oy : 0));
}

static void gfx_drawer_release(mrb_gfx_data *data)
{
    if (data->drawer >= 0 && data->owner && data->owner == fmrb_current()) {
        fmrb_gfx_drawer_close(data->drawer);
    }
    data->drawer = -1;
    data->owner = NULL;
}

static int32_t gfx_drawer_take(mrb_state *mrb, mrb_gfx_data *data, int32_t canvas)
{
    int32_t d = fmrb_gfx_drawer_open(canvas, 0, 0);
    if (d < 0) {
        return d;
    }
    data->drawer = d;
    data->owner = fmrb_current();
    (void)mrb;
    return d;
}

static void mrb_gfx_data_free(mrb_state *mrb, void *ptr)
{
    if (ptr) {
        // Don't deinitialize global context, just free the wrapper (and its
        // drawer). The global context is managed by fmrb_gfx layer
        gfx_drawer_release((mrb_gfx_data *)ptr);
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
    data->drawer = -1;

    // Get global graphics context (already initialized by kernel/host)
    data->ctx = fmrb_gfx_get_global_context();
    if (!data->ctx) {
        FMRB_LOGE(TAG, "Global graphics context not initialized");
        mrb_free(mrb, data);
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics context not initialized");
    }

    // The drawer for this instance, on its canvas
    if (gfx_drawer_take(mrb, data, (int32_t)canvas_id) < 0) {
        mrb_free(mrb, data);
        mrb_raisef(mrb, E_RUNTIME_ERROR, "no room for another FmrbGfx (%d at most)",
                   (int)FMRB_GFX_DRAWERS_MAX);
    }

    FMRB_LOGI(TAG, "FmrbGfx initialized: canvas_id=%d, drawer=%d, ctx=%p",
              (int)canvas_id, (int)data->drawer, data->ctx);

    mrb_data_init(self, data, &mrb_gfx_data_type);
    return self;
}

// The receiver number of the app boundary's FmrbGfx functions: this
// object's drawer (fmrb_gfx_drawer.h), which has the canvas and the origin.
static int32_t fmrb_mrb_recv_gfx(mrb_state *mrb, mrb_value self)
{
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx || data->drawer < 0) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }
    return data->drawer;
}

// FmrbGfx methods generated from sig/ (tools/boundary/gen.rb): every
// method of the class on the app boundary (sig/fmrb_gfx.rbs). The Ruby
// wrappers (draw_text, set_font ...) are in mrblib/fmrb-gfx.rb.
#define FMRB_GEN_MRB_FmrbGfx
#include "fmrb_gen/fmrb_boundary_mrb.inc"

// FmrbGfx#present([x, y]): the wrapper of present_raw, written in C here
// rather than in mrblib (doc/app_boundary, decided after W4b-1). An app calls
// it once a frame; in Ruby it cost twice the VM instructions of the C method
// it had been before W4a. Without a position (or with either nil) the canvas
// goes where the app's window is, as the Ruby wrapper did.
static mrb_value mrb_gfx_present(mrb_state *mrb, mrb_value self)
{
    mrb_value x = mrb_nil_value();
    mrb_value y = mrb_nil_value();
    mrb_get_args(mrb, "|oo", &x, &y);
    int32_t gfx = fmrb_mrb_recv_gfx(mrb, self);
    int32_t ret;
    if (mrb_test(x) && mrb_test(y)) {
        ret = fmrb_bnd_gfx_present_raw(gfx, (int32_t)mrb_as_int(mrb, x), (int32_t)mrb_as_int(mrb, y), 1);
    } else {
        ret = fmrb_bnd_gfx_present_raw(gfx, 0, 0, 0);
    }
    fmrb_gen_mrb_check(mrb, ret, "present");
    return self;
}

// Graphics#destroy - Explicitly release graphics resources
static mrb_value mrb_gfx_destroy(mrb_state *mrb, mrb_value self)
{
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (data && data->ctx) {
        data->ctx = NULL;
        gfx_drawer_release(data);
    }
    return mrb_nil_value();
}

// ============================================================
// Video (not on the boundary yet: FmrbVideo has no sig)
// ============================================================

// FmrbGfx#_video_open(path, x, y, fps, loop) -> Hash {width:, height:} or nil
// Starts (paused) a motion-JPEG file playing into this canvas at (x, y).
// Modern/P4 only; any other backend answers with an error and this returns nil.
static mrb_value mrb_gfx_video_open(mrb_state *mrb, mrb_value self)
{
    char *path;
    mrb_int x, y, fps;
    mrb_bool loop;
    mrb_get_args(mrb, "ziiib", &path, &x, &y, &fps, &loop);

#if !defined(FMRB_HW_MODERN) && !defined(FMRB_PLATFORM_HOSTED)
    // Retro has no JPEG decoder and its display backend cannot read this
    // board's filesystem. Answer immediately instead of letting the app wait
    // out a sync timeout on a command the backend will never know.
    //
    // The browser is admitted alongside the P4: its display backend is the
    // same display task, and wasm/backend/display_video_wasm.c plays the file
    // with TJpgDec where the P4 uses its hardware engine. Note this is NOT
    // FMRB_HW_FAMILY_MODERN, which would also let the Linux simulator in --
    // there the display is another process that knows nothing of these
    // commands, so every call would cost the full 10 s sync timeout instead
    // of failing here.
    (void)self; (void)path; (void)x; (void)y; (void)fps; (void)loop;
    return mrb_nil_value();
#else

    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    gfx_at_t at;
    if (!data || !data->ctx || !gfx_at(data, &at)) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    }

    size_t path_len = strlen(path);
    if (path_len >= 120) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Path too long");
    }

    uint8_t payload_buf[sizeof(fmrb_link_graphics_video_open_t) + 120];
    size_t payload_len = sizeof(fmrb_link_graphics_video_open_t) + path_len;
    fmrb_link_graphics_video_open_t *hdr =
        (fmrb_link_graphics_video_open_t *)payload_buf;
    hdr->canvas_id = at.canvas;
    hdr->x         = (int16_t)(x + at.ox);
    hdr->y         = (int16_t)(y + at.oy);
    hdr->fps       = (uint16_t)fps;
    hdr->flags     = loop ? FMRB_LINK_GFX_VIDEO_FLAG_LOOP : 0;
    hdr->path_len  = (uint16_t)path_len;
    memcpy(payload_buf + sizeof(fmrb_link_graphics_video_open_t), path, path_len);

    uint8_t resp_buf[sizeof(fmrb_link_graphics_video_opened_t)];
    uint32_t resp_len = sizeof(resp_buf);
    fmrb_err_t ret = fmrb_transport_send_sync(
        FMRB_LINK_TYPE_GRAPHICS,
        FMRB_LINK_GFX_VIDEO_OPEN,
        payload_buf, payload_len,
        resp_buf, &resp_len,
        10000);
    if (ret != FMRB_OK || resp_len < sizeof(fmrb_link_graphics_video_opened_t)) {
        FMRB_LOGE(TAG, "video_open failed: %d (path=%s)", ret, path);
        return mrb_nil_value();
    }

    fmrb_link_graphics_video_opened_t *resp =
        (fmrb_link_graphics_video_opened_t *)resp_buf;
    if (resp->result != 0) {
        FMRB_LOGE(TAG, "video_open rejected: %u (path=%s)", resp->result, path);
        return mrb_nil_value();
    }

    mrb_value hash = mrb_hash_new(mrb);
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "width")),
                 mrb_fixnum_value(resp->width));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "height")),
                 mrb_fixnum_value(resp->height));
    return hash;
#endif  /* FMRB_HW_MODERN */
}

// Shared tail for _video_control / _video_status: unpack the status record.
static mrb_value video_status_hash(mrb_state *mrb, const uint8_t *buf, uint32_t len)
{
    if (len < sizeof(fmrb_link_graphics_video_status_t)) {
        FMRB_LOGW(TAG, "video status: %u bytes, need %u",
                  (unsigned)len, (unsigned)sizeof(fmrb_link_graphics_video_status_t));
        return mrb_nil_value();
    }
    const fmrb_link_graphics_video_status_t *st =
        (const fmrb_link_graphics_video_status_t *)buf;
    mrb_value hash = mrb_hash_new(mrb);
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "state")),
                 mrb_fixnum_value(st->state));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "shown")),
                 mrb_fixnum_value((mrb_int)st->frames_shown));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "dropped")),
                 mrb_fixnum_value((mrb_int)st->frames_dropped));
    return hash;
}

// FmrbGfx#_video_control(action) -> Hash status or nil
static mrb_value mrb_gfx_video_control(mrb_state *mrb, mrb_value self)
{
    mrb_int action;
    mrb_get_args(mrb, "i", &action);
    (void)self;

#if !defined(FMRB_HW_MODERN) && !defined(FMRB_PLATFORM_HOSTED)
    (void)action;
    return mrb_nil_value();
#else

    fmrb_link_graphics_video_control_t payload;
    payload.action = (uint8_t)action;

    uint8_t resp_buf[sizeof(fmrb_link_graphics_video_status_t)];
    uint32_t resp_len = sizeof(resp_buf);
    fmrb_err_t ret = fmrb_transport_send_sync(
        FMRB_LINK_TYPE_GRAPHICS,
        FMRB_LINK_GFX_VIDEO_CONTROL,
        (uint8_t *)&payload, sizeof(payload),
        resp_buf, &resp_len, 5000);
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "video control: send failed (%d)", (int)ret);
        return mrb_nil_value();
    }
    return video_status_hash(mrb, resp_buf, resp_len);
#endif  /* FMRB_HW_MODERN */
}

// FmrbGfx#_video_status -> Hash status or nil
static mrb_value mrb_gfx_video_status(mrb_state *mrb, mrb_value self)
{
    (void)self;
#if !defined(FMRB_HW_MODERN) && !defined(FMRB_PLATFORM_HOSTED)
    return mrb_nil_value();
#else
    uint8_t resp_buf[sizeof(fmrb_link_graphics_video_status_t)];
    uint32_t resp_len = sizeof(resp_buf);
    fmrb_err_t ret = fmrb_transport_send_sync(
        FMRB_LINK_TYPE_GRAPHICS,
        FMRB_LINK_GFX_VIDEO_STATUS,
        NULL, 0,
        resp_buf, &resp_len, 5000);
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "video status: send failed (%d)", (int)ret);
        return mrb_nil_value();
    }
    return video_status_hash(mrb, resp_buf, resp_len);
#endif  /* FMRB_HW_MODERN */
}

// FmrbGfx#_create_mask(width, height, data) -> Integer
//
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

// --- Sprite API bindings ---

static mrb_value mrb_gfx_create_sprite_image(mrb_state *mrb, mrb_value self)
{
    mrb_int width, height, trans_color, use_trans;
    mrb_get_args(mrb, "iiii", &width, &height, &trans_color, &use_trans);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    uint16_t id = fmrb_gfx_create_sprite_image(data->ctx, gfx_canvas(data),
        (uint16_t)width, (uint16_t)height, (uint8_t)trans_color, use_trans != 0);
    if (id == 0) mrb_raise(mrb, E_RUNTIME_ERROR, "Failed to create sprite image");
    return mrb_fixnum_value(id);
}

// FmrbGfx#_load_sprite_image_bmp(image_id, path)
static mrb_value mrb_gfx_load_sprite_image_bmp(mrb_state *mrb, mrb_value self)
{
    mrb_int image_id;
    char *path;
    mrb_get_args(mrb, "iz", &image_id, &path);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) return self;

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_load_sprite_image_bmp(&cmd, gfx_canvas(data), (uint16_t)image_id,
                                       path);

    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to load BMP: %d", ret);
    }
    return self;
}

// FmrbGfx#_export_frame(path)
//
// Queued behind whatever has already been sent, so the picture written is the
// one the last present composited. Nothing is returned and nothing waits: the
// caller learns the write finished by finding the file (Tab5, where core and
// display share a filesystem) or by waiting a fixed time (the simulator,
// where they do not).
static mrb_value mrb_gfx_export_frame(mrb_state *mrb, mrb_value self)
{
    char *path;
    mrb_get_args(mrb, "z", &path);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) return self;

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_export_frame(&cmd, gfx_canvas(data), path);

    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to export frame: %d", ret);
    }
    return self;
}

static mrb_value mrb_gfx_delete_sprite_image(mrb_state *mrb, mrb_value self)
{
    mrb_int image_id;
    mrb_get_args(mrb, "i", &image_id);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) return self;

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_sprite_image(&cmd, gfx_canvas(data), (uint16_t)image_id);
    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "delete_sprite_image fmrb_gfx_submit failed: %d", ret);
    }
    return self;
}

static mrb_value mrb_gfx_set_sprite_image_target(mrb_state *mrb, mrb_value self)
{
    mrb_int image_id;
    mrb_get_args(mrb, "i", &image_id);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) return self;

    // Route through the host_task GFX queue so this target switch stays in
    // order with the surrounding pixel commands. Bypassing the queue (calling
    // fmrb_gfx_set_sprite_image_target directly) lets target=0 race past the
    // tail of a sprite draw and clips the bottom-right pixels.
    fmrb_gfx_drawer_set_to_image(data->drawer, image_id != 0);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_set_sprite_image_target(&cmd, gfx_canvas(data),
                                         (uint16_t)image_id);
    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "set_sprite_image_target fmrb_gfx_submit failed: %d", ret);
    }
    return self;
}

static mrb_value mrb_gfx_create_sprite_instance(mrb_state *mrb, mrb_value self)
{
    mrb_value ary;
    mrb_int x, y, z_order;
    mrb_get_args(mrb, "Aiii", &ary, &x, &y, &z_order);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) mrb_raise(mrb, E_RUNTIME_ERROR, "Graphics not initialized");
    mrb_int frame_count = RARRAY_LEN(ary);
    if (frame_count <= 0 || frame_count > FMRB_SPRITE_MAX_FRAMES)
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid frame count");
    uint16_t image_ids[FMRB_SPRITE_MAX_FRAMES];
    for (mrb_int i = 0; i < frame_count; i++)
        image_ids[i] = (uint16_t)mrb_fixnum(mrb_ary_ref(mrb, ary, i));
    uint16_t id = fmrb_gfx_create_sprite_instance(data->ctx, gfx_canvas(data),
        image_ids, (uint8_t)frame_count, gfx_sprite_x(data, x), gfx_sprite_y(data, y), (int16_t)z_order);
    if (id == 0) mrb_raise(mrb, E_RUNTIME_ERROR, "Failed to create sprite instance");
    return mrb_fixnum_value(id);
}

static mrb_value mrb_gfx_delete_sprite_instance(mrb_state *mrb, mrb_value self)
{
    mrb_int instance_id;
    mrb_get_args(mrb, "i", &instance_id);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) return self;

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_sprite_instance(&cmd, gfx_canvas(data),
                                        (uint16_t)instance_id);
    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "delete_sprite_instance fmrb_gfx_submit failed: %d", ret);
    }
    return self;
}

static mrb_value mrb_gfx_sprite_move(mrb_state *mrb, mrb_value self)
{
    mrb_int instance_id, x, y;
    mrb_get_args(mrb, "iii", &instance_id, &x, &y);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) return self;

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_sprite_instance_move(&cmd, gfx_canvas(data),
                                      (uint16_t)instance_id, gfx_sprite_x(data, x),
                                      gfx_sprite_y(data, y));
    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "sprite_move fmrb_gfx_submit failed: %d", ret);
    }
    return self;
}

static mrb_value mrb_gfx_sprite_visible(mrb_state *mrb, mrb_value self)
{
    mrb_int instance_id;
    mrb_bool visible;
    mrb_get_args(mrb, "ib", &instance_id, &visible);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) return self;

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_sprite_instance_set_visible(&cmd, gfx_canvas(data),
                                             (uint16_t)instance_id, visible);
    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "sprite_visible fmrb_gfx_submit failed: %d", ret);
    }
    return self;
}

static mrb_value mrb_gfx_sprite_frame(mrb_state *mrb, mrb_value self)
{
    mrb_int instance_id, frame_index;
    mrb_get_args(mrb, "ii", &instance_id, &frame_index);
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) return self;

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_sprite_instance_set_frame(&cmd, gfx_canvas(data),
                                           (uint16_t)instance_id,
                                           (uint8_t)frame_index);
    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "sprite_frame fmrb_gfx_submit failed: %d", ret);
    }
    return self;
}

static mrb_value mrb_gfx_delete_all_sprites(mrb_state *mrb, mrb_value self)
{
    mrb_gfx_data *data = (mrb_gfx_data *)mrb_data_get_ptr(mrb, self, &mrb_gfx_data_type);
    if (!data || !data->ctx) return self;

    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_all_sprites(&cmd, gfx_canvas(data));
    fmrb_err_t ret = fmrb_gfx_submit(&cmd);
    if (ret != FMRB_OK) {
        FMRB_LOGE(TAG, "delete_all_sprites fmrb_gfx_submit failed: %d", ret);
    }
    return self;
}

// FmrbGfx#_set_origin(x, y, w, h)
// The area this FmrbGfx draws in: its top-left on the canvas and its size,
// kept by its drawer (origin_x, area_width ... are on the app boundary).
// FmrbApp lays it out (_layout_user_area); nothing else needs to call it.
static mrb_value mrb_gfx_set_origin(mrb_state *mrb, mrb_value self)
{
    mrb_int x, y, w, h;
    mrb_get_args(mrb, "iiii", &x, &y, &w, &h);
    mrb_gfx_data *data = (mrb_gfx_data *)DATA_PTR(self);
    if (!data) return self;
    fmrb_gfx_drawer_set_origin(data->drawer, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h);
    return self;
}

// The origin of an FmrbGfx, for app.c to put a pointer event into the same
// coordinates the app draws in. false (and 0, 0) when gfx is not one.
bool fmrb_gfx_origin_of(mrb_state *mrb, mrb_value gfx, int *ox, int *oy)
{
    *ox = 0;
    *oy = 0;
    if (!mrb_data_p(gfx) || DATA_TYPE(gfx) != &mrb_gfx_data_type) return false;
    mrb_gfx_data *data = (mrb_gfx_data *)DATA_PTR(gfx);
    const fmrb_gfx_drawer_t *d = data ? fmrb_gfx_drawer_get(data->drawer) : NULL;
    if (!d) return false;
    *ox = d->ox;
    *oy = d->oy;
    return true;
}

#ifdef FMRB_HAVE_CHECKPOINT
// ============================================================
// Carrying an FmrbGfx in a checkpoint image (doc/checkpoint F3, D7, D8)
// ============================================================
//
// The C data of an FmrbGfx is a handle on the program's graphics context and
// a canvas id, both of this program only. The image records which of the
// app's canvases the object drew on (its role: the window, the wallpaper, an
// extra canvas and how that was made, or none) instead. The restored object
// draws on nothing until FmrbApp's restore (fmrb-app.rb, _checkpoint_restored)
// has made the app's canvases again from its context and asks each FmrbGfx to
// bind itself (_checkpoint_rebind). What was drawn is not carried: the app
// draws it again (on_restore, report/f3.md).
//
// Registered only by the VMs that open a checkpoint (D7, report/f0.md 5):
// fmrb_app_checkpoint_vm_opened calls fmrb_gfx_checkpoint_register.

#define GFX_CKPT_VERSION 1
enum {
    GFX_CKPT_ROLE_NONE = 0,      // made in this program (not restored)
    GFX_CKPT_ROLE_WINDOW = 1,
    GFX_CKPT_ROLE_WALLPAPER = 2,
    GFX_CKPT_ROLE_EXTRA = 3,
    GFX_CKPT_ROLE_DEAD = 4,      // destroyed, or on no canvas of the app
};

static mrb_int gfx_iv_int(mrb_state *mrb, mrb_value obj, const char *name, mrb_int dflt)
{
    mrb_value v = mrb_iv_get(mrb, obj, mrb_intern_cstr(mrb, name));
    return mrb_fixnum_p(v) ? mrb_fixnum(v) : dflt;
}

// version u8, role u8, then for an extra canvas: width u16, height u16,
// z offset i16, transparent u8, colour key u8 (big-endian)
static int gfx_ckpt_dump(mrb_state *mrb, mrb_value obj, mrb_checkpoint_buf *out)
{
    mrb_gfx_data *d = (mrb_gfx_data *)DATA_PTR(obj);
    fmrb_app_task_context_t *ctx = fmrb_current();
    uint8_t b[10];
    size_t n = 0;
    uint8_t role = GFX_CKPT_ROLE_DEAD;
    b[n++] = GFX_CKPT_VERSION;
    fmrb_canvas_handle_t canvas = (d && d->ctx) ? gfx_canvas(d) : FMRB_CANVAS_SCREEN;
    if (d && d->ctx && ctx && canvas != FMRB_CANVAS_SCREEN) {
        if (canvas == ctx->canvas_id) {
            role = GFX_CKPT_ROLE_WINDOW;
        } else if (canvas == ctx->bg_canvas_id) {
            role = GFX_CKPT_ROLE_WALLPAPER;
        } else {
            for (int i = 0; i < FMRB_APP_MAX_EXTRA_CANVAS; i++) {
                if (ctx->extra_canvas_ids[i] == canvas) role = GFX_CKPT_ROLE_EXTRA;
            }
        }
    }
    b[n++] = role;
    if (role == GFX_CKPT_ROLE_EXTRA) {
        // How FmrbApp#create_canvas_gfx made it (@_extra = [z, transparent, key]).
        mrb_value spec = mrb_iv_get(mrb, obj, mrb_intern_lit(mrb, "@_extra"));
        mrb_int w = gfx_iv_int(mrb, obj, "@canvas_width", 0);
        mrb_int h = gfx_iv_int(mrb, obj, "@canvas_height", 0);
        if (!mrb_array_p(spec) || RARRAY_LEN(spec) != 3 || w <= 0 || h <= 0) {
            FMRB_LOGW(TAG, "an extra canvas not made by create_canvas_gfx cannot be carried");
            return -1;
        }
        mrb_int z = mrb_fixnum_p(RARRAY_PTR(spec)[0]) ? mrb_fixnum(RARRAY_PTR(spec)[0]) : 1;
        mrb_int t = mrb_fixnum_p(RARRAY_PTR(spec)[1]) ? mrb_fixnum(RARRAY_PTR(spec)[1]) : 0;
        mrb_int k = mrb_fixnum_p(RARRAY_PTR(spec)[2]) ? mrb_fixnum(RARRAY_PTR(spec)[2]) : 0;
        b[n++] = (uint8_t)(w >> 8); b[n++] = (uint8_t)w;
        b[n++] = (uint8_t)(h >> 8); b[n++] = (uint8_t)h;
        b[n++] = (uint8_t)((uint16_t)z >> 8); b[n++] = (uint8_t)z;
        b[n++] = (uint8_t)(t ? 1 : 0);
        b[n++] = (uint8_t)k;
    }
    mrb_checkpoint_buf_bytes(mrb, out, b, n);
    return 0;
}

static void *gfx_ckpt_restore(mrb_state *mrb, const uint8_t *p, size_t len)
{
    if (len < 2 || p[0] != GFX_CKPT_VERSION) {
        FMRB_LOGW(TAG, "an FmrbGfx of an image this build does not read (%u bytes)", (unsigned)len);
        return NULL;   // an FmrbGfx on nothing: drawing raises, as on a destroyed one
    }
    mrb_gfx_data *d = (mrb_gfx_data *)mrb_malloc(mrb, sizeof(mrb_gfx_data));
    memset(d, 0, sizeof(*d));
    d->drawer = -1;   // none until _checkpoint_rebind
    d->ckpt_role = p[1];
    if (d->ckpt_role == GFX_CKPT_ROLE_EXTRA) {
        if (len < 10) {
            d->ckpt_role = GFX_CKPT_ROLE_DEAD;
        } else {
            d->ckpt_width = (uint16_t)((p[2] << 8) | p[3]);
            d->ckpt_height = (uint16_t)((p[4] << 8) | p[5]);
            d->ckpt_z_offset = (int16_t)((p[6] << 8) | p[7]);
            d->ckpt_transparent = p[8];
            d->ckpt_key = p[9];
        }
    }
    return d;
}

void fmrb_gfx_checkpoint_register(mrb_state *mrb)
{
    mrb_checkpoint_register_data(mrb, &mrb_gfx_data_type, "FmrbGfx", gfx_ckpt_dump, gfx_ckpt_restore);
}

// FmrbGfx#_checkpoint_rebind -> Integer (the canvas id) or nil
// Binds a restored FmrbGfx to the canvas of its role in this program: the
// window and the wallpaper are the context's (FmrbApp#_checkpoint_canvases
// made them), an extra canvas is made again as it was. One made here, or one
// on nothing, is left as it is.
static mrb_value mrb_gfx_checkpoint_rebind(mrb_state *mrb, mrb_value self)
{
    mrb_gfx_data *d = (mrb_gfx_data *)DATA_PTR(self);
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!d || !ctx || d->ctx || d->ckpt_role == GFX_CKPT_ROLE_NONE) return mrb_nil_value();
    fmrb_canvas_handle_t id = FMRB_CANVAS_SCREEN;
    switch (d->ckpt_role) {
        case GFX_CKPT_ROLE_WINDOW:    id = ctx->canvas_id; break;
        case GFX_CKPT_ROLE_WALLPAPER: id = ctx->bg_canvas_id; break;
        case GFX_CKPT_ROLE_EXTRA:
            if (fmrb_app_canvas_create_extra(ctx, d->ckpt_width, d->ckpt_height, d->ckpt_z_offset,
                                             d->ckpt_transparent != 0, d->ckpt_key, &id) != FMRB_OK) {
                id = FMRB_CANVAS_SCREEN;
            }
            break;
        default: break;
    }
    d->ckpt_role = GFX_CKPT_ROLE_NONE;
    if (id == FMRB_CANVAS_SCREEN) return mrb_nil_value();   // stays on nothing
    if (!fmrb_gfx_get_global_context()) return mrb_nil_value();
    // A drawer of this program on that canvas (the origin comes after, from
    // FmrbApp's layout). The image's own canvas id, an ivar of the builds
    // before the drawers, is not read.
    if (gfx_drawer_take(mrb, d, (int32_t)id) < 0) return mrb_nil_value();
    d->ctx = fmrb_gfx_get_global_context();
    return mrb_fixnum_value(id);
}
// FmrbGfx#_checkpoint_keep? -> bool
// Whether this app can be written to an image (movable, or started from one):
// only then does a GfxBlock keep its program to define it again (gfx_block.rb).
static mrb_value mrb_gfx_checkpoint_keep_p(mrb_state *mrb, mrb_value self)
{
    (void)self;
    fmrb_app_task_context_t *ctx = fmrb_current();
    return mrb_bool_value(ctx && (ctx->movable || ctx->load_mode == FMRB_LOAD_MODE_CHECKPOINT));
}
#endif /* FMRB_HAVE_CHECKPOINT */

void mrb_fmrb_gfx_init(mrb_state *mrb)
{
    struct RClass *gfx_class = mrb_define_class(mrb, "FmrbGfx", mrb->object_class);
    MRB_SET_INSTANCE_TT(gfx_class, MRB_TT_DATA);

    mrb_define_method(mrb, gfx_class, "_init", mrb_gfx_initialize, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, gfx_class, "_set_origin", mrb_gfx_set_origin, MRB_ARGS_REQ(4));
    // Everything on the app boundary (sig/fmrb_gfx.rbs), generated.
    FMRB_GEN_MRB_DEFINE_FmrbGfx(mrb, gfx_class);
    mrb_define_method(mrb, gfx_class, "present", mrb_gfx_present, MRB_ARGS_OPT(2));
    mrb_define_method(mrb, gfx_class, "destroy", mrb_gfx_destroy, MRB_ARGS_NONE());
#ifdef FMRB_HAVE_CHECKPOINT
    mrb_define_method(mrb, gfx_class, "_checkpoint_rebind", mrb_gfx_checkpoint_rebind, MRB_ARGS_NONE());
    mrb_define_method(mrb, gfx_class, "_checkpoint_keep?", mrb_gfx_checkpoint_keep_p, MRB_ARGS_NONE());
#endif

    // Motion-JPEG playback (Modern/P4 only; nil elsewhere)
    mrb_define_method(mrb, gfx_class, "_video_open", mrb_gfx_video_open, MRB_ARGS_REQ(5));
    mrb_define_method(mrb, gfx_class, "_video_control", mrb_gfx_video_control, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, gfx_class, "_video_status", mrb_gfx_video_status, MRB_ARGS_NONE());

    // Sprite API
    mrb_define_method(mrb, gfx_class, "_create_sprite_image", mrb_gfx_create_sprite_image, MRB_ARGS_REQ(4));
    mrb_define_method(mrb, gfx_class, "_delete_sprite_image", mrb_gfx_delete_sprite_image, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, gfx_class, "_load_sprite_image_bmp", mrb_gfx_load_sprite_image_bmp, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, gfx_class, "_export_frame", mrb_gfx_export_frame, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, gfx_class, "_set_sprite_image_target", mrb_gfx_set_sprite_image_target, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, gfx_class, "_create_sprite_instance", mrb_gfx_create_sprite_instance, MRB_ARGS_REQ(4));
    mrb_define_method(mrb, gfx_class, "_delete_sprite_instance", mrb_gfx_delete_sprite_instance, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, gfx_class, "_sprite_move", mrb_gfx_sprite_move, MRB_ARGS_REQ(3));
    mrb_define_method(mrb, gfx_class, "_sprite_visible", mrb_gfx_sprite_visible, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, gfx_class, "_sprite_frame", mrb_gfx_sprite_frame, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, gfx_class, "_delete_all_sprites", mrb_gfx_delete_all_sprites, MRB_ARGS_NONE());

    // Color utility class methods
    mrb_define_class_method(mrb, gfx_class, "hsv_to_rgb", mrb_gfx_s_hsv_to_rgb, MRB_ARGS_REQ(3));

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
