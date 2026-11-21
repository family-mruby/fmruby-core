/*
 * The app boundary's FmrbGfx flat functions (doc/app_boundary): the C bodies
 * behind fmrb_gen/fmrb_boundary.h, generated from the .rbs files in sig/.
 * Every engine's glue (mruby, Lua, MicroPython, Spinel, WAMR) is generated
 * and calls these; nothing here knows an engine.
 *
 * The receiver (the first argument) is a drawer of the calling app
 * (fmrb_gfx_drawer.h): its canvas, its drawing origin and area, and whether
 * it draws into a sprite image. Coordinates of a drawing call are from that
 * origin (none while it draws into a sprite image); clear, present and the
 * canvas-wide settings ignore it. The drawers are opened, laid out and closed
 * here too: the engines' FmrbGfx keep only the number (doc/app_boundary W4b).
 * Results follow fmrb_gen/fmrb_boundary.h: 0 or more on
 * success, FMRB_BOUNDARY_ERR_FAILED, or FMRB_BOUNDARY_ERR_ARG for an
 * argument out of range.
 *
 * The bodies are those the mruby binding (picoruby-fmrb-app gfx.c) and the
 * Spinel shim (fmrb_spx_gfx.c) had before W4a, with the receiver resolved
 * here instead of in each engine.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "fmrb_gen/fmrb_boundary.h"
#include "fmrb_gfx_drawer.h"
#include "fmrb_app.h"
#include "fmrb_err.h"
#include "fmrb_gfx.h"
#include "fmrb_gfx_cmd.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_msg.h"
#include "fmrb_mem.h"
#include "fmrb_rtos.h"
#include "fmrb_hal.h"
#include "fmrb_hal_time.h"
#include "fmrb_task_config.h"
#include "fmrb_link_protocol.h"
#include "fmrb_transport.h"
#include "fmrb_file_transfer_msg.h"
#include "fmrb_kernel.h"
#include "fmrb_theme.h"
#include "fmrb_picture332.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_heap_caps.h"
#endif

#define ERR_FAILED FMRB_BOUNDARY_ERR_FAILED
#define ERR_ARG    FMRB_BOUNDARY_ERR_ARG

/* The longest path the graphics side takes (file_cmd_t.path). */
#define GFX_PATH_MAX 120

/* ---- the drawers --------------------------------------------------------- */

// A drawer of the given app context (the caller has looked it up).
static fmrb_gfx_drawer_t *drawer_in(fmrb_app_task_context_t *ctx, int32_t drawer)
{
    if (drawer < 0 || drawer >= FMRB_GFX_DRAWERS_MAX) {
        return NULL;
    }
    if (!ctx || !ctx->gfx_drawers[drawer].used) {
        return NULL;
    }
    return &ctx->gfx_drawers[drawer];
}

static fmrb_gfx_drawer_t *drawer_of(int32_t drawer)
{
    return drawer_in(fmrb_current(), drawer);
}

static uint16_t clamp_size(int32_t v)
{
    return (uint16_t)(v < 0 ? 0 : (v > 0xFFFF ? 0xFFFF : v));
}

int32_t fmrb_gfx_drawer_open(int32_t canvas, int32_t area_w, int32_t area_h)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return ERR_FAILED;
    }
    for (int32_t i = 0; i < FMRB_GFX_DRAWERS_MAX; i++) {
        fmrb_gfx_drawer_t *d = &ctx->gfx_drawers[i];
        if (!d->used) {
            memset(d, 0, sizeof(*d));
            d->used = 1;
            // A headless MicroPython app's -1 stays what the canvas handle
            // type makes of it, as before the drawers.
            d->canvas = (uint16_t)canvas;
            d->area_w = clamp_size(area_w);
            d->area_h = clamp_size(area_h);
            return i;
        }
    }
    return ERR_FAILED;
}

void fmrb_gfx_drawer_close(int32_t drawer)
{
    fmrb_gfx_drawer_t *d = drawer_of(drawer);
    if (d) {
        d->used = 0;
    }
}

int32_t fmrb_gfx_drawer_set_origin(int32_t drawer, int32_t x, int32_t y, int32_t w, int32_t h)
{
    fmrb_gfx_drawer_t *d = drawer_of(drawer);
    if (!d) {
        return ERR_FAILED;
    }
    d->ox = (int16_t)x;
    d->oy = (int16_t)y;
    d->area_w = clamp_size(w);
    d->area_h = clamp_size(h);
    return 0;
}

int32_t fmrb_gfx_drawer_set_canvas(int32_t drawer, int32_t canvas)
{
    fmrb_gfx_drawer_t *d = drawer_of(drawer);
    if (!d) {
        return ERR_FAILED;
    }
    d->canvas = (uint16_t)canvas;
    return 0;
}

int32_t fmrb_gfx_drawer_set_to_image(int32_t drawer, int32_t to_image)
{
    fmrb_gfx_drawer_t *d = drawer_of(drawer);
    if (!d) {
        return ERR_FAILED;
    }
    d->to_image = to_image ? 1 : 0;
    return 0;
}

const fmrb_gfx_drawer_t *fmrb_gfx_drawer_get(int32_t drawer)
{
    return drawer_of(drawer);
}

typedef struct {
    fmrb_app_task_context_t *ctx;  // the calling app's, looked up once per call
    fmrb_canvas_handle_t canvas;
    int16_t ox, oy;   // what a drawing call adds: the origin, or 0 into an image
    int16_t ax, ay;   // the origin itself (the sprite clip's)
} gfx_target_t;

static bool target_of(int32_t gfx, gfx_target_t *t)
{
    t->ctx = fmrb_current();
    const fmrb_gfx_drawer_t *d = drawer_in(t->ctx, gfx);
    if (!d) {
        return false;
    }
    t->canvas = (fmrb_canvas_handle_t)d->canvas;
    t->ax = d->ox;
    t->ay = d->oy;
    t->ox = d->to_image ? 0 : d->ox;
    t->oy = d->to_image ? 0 : d->oy;
    return true;
}

#define TARGET(gfx, t) \
    gfx_target_t t; \
    if (!target_of((gfx), &t)) return ERR_FAILED

#define TX(t, v) ((int16_t)((v) + (t).ox))
#define TY(t, v) ((int16_t)((v) + (t).oy))

/* ---- the drawer's state -------------------------------------------------- */

int32_t fmrb_bnd_gfx_canvas_id(int32_t gfx)
{
    const fmrb_gfx_drawer_t *d = drawer_of(gfx);
    return d ? (int32_t)d->canvas : ERR_FAILED;
}

int32_t fmrb_bnd_gfx_origin_x(int32_t gfx)
{
    const fmrb_gfx_drawer_t *d = drawer_of(gfx);
    return d ? (int32_t)d->ox : ERR_FAILED;
}

int32_t fmrb_bnd_gfx_origin_y(int32_t gfx)
{
    const fmrb_gfx_drawer_t *d = drawer_of(gfx);
    return d ? (int32_t)d->oy : ERR_FAILED;
}

int32_t fmrb_bnd_gfx_area_width(int32_t gfx)
{
    const fmrb_gfx_drawer_t *d = drawer_of(gfx);
    return d ? (int32_t)d->area_w : ERR_FAILED;
}

int32_t fmrb_bnd_gfx_area_height(int32_t gfx)
{
    const fmrb_gfx_drawer_t *d = drawer_of(gfx);
    return d ? (int32_t)d->area_h : ERR_FAILED;
}

// Queue a command for the app whose drawer TARGET found (its context, not
// looked up again).
static int32_t submit(const gfx_target_t *t, const gfx_cmd_t *cmd)
{
    return fmrb_gfx_submit_from(t->ctx, cmd) == FMRB_OK ? 0 : ERR_FAILED;
}

/* A Ruby string (not NUL terminated) as a C path. */
static bool copy_path(char *dst, size_t cap, const char *src, int32_t len)
{
    if (!src || len <= 0 || (size_t)len >= cap) {
        return false;
    }
    memcpy(dst, src, (size_t)len);
    dst[len] = '\0';
    return true;
}

/* ---- shapes -------------------------------------------------------------- */

int32_t fmrb_bnd_gfx_clear(int32_t gfx, int32_t color)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_clear(&cmd, t.canvas, (fmrb_color_t)color);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_set_pixel(int32_t gfx, int32_t x, int32_t y, int32_t color)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_pixel(&cmd, t.canvas, TX(t, x), TY(t, y), (fmrb_color_t)color);
    return submit(&t, &cmd);
}

// A synchronous command through host_task, so every queued drawing command
// reaches the display first ("draw, then read what I drew"). 0 for a point
// outside the canvas.
int32_t fmrb_bnd_gfx_get_pixel(int32_t gfx, int32_t x, int32_t y)
{
    TARGET(gfx, t);
    fmrb_link_graphics_pixel_value_t resp = { .color = 0, .status = 0xFF };
    gfx_cmd_sync_ctx_t sync = {
        .done = fmrb_semaphore_create_binary(),
        .response_buf = (uint8_t *)&resp,
        .response_len = sizeof(resp),
        .result = -1,
    };
    if (!sync.done) {
        return ERR_FAILED;
    }
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_get_pixel(&cmd, t.canvas, TX(t, x), TY(t, y), &sync);
    if (fmrb_gfx_submit_from(t.ctx, &cmd) != FMRB_OK) {
        fmrb_semaphore_delete(sync.done);
        return ERR_FAILED;
    }
    // The reply context is on this stack; hold off a forced delete.
    fmrb_app_sync_io_begin();
    fmrb_base_type_t took = fmrb_semaphore_take(sync.done, FMRB_MS_TO_TICKS(5000));
    fmrb_app_sync_io_end();
    fmrb_semaphore_delete(sync.done);
    if (took != FMRB_TRUE) {
        return ERR_FAILED;
    }
    if (sync.result != 0 || sync.response_len < sizeof(resp) || resp.status == 0xFF) {
        return 0;
    }
    return (int32_t)resp.color;
}

int32_t fmrb_bnd_gfx_draw_line(int32_t gfx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t color)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_line(&cmd, t.canvas, TX(t, x0), TY(t, y0), TX(t, x1), TY(t, y1), (fmrb_color_t)color);
    return submit(&t, &cmd);
}

static int32_t rect(int32_t gfx, int32_t x, int32_t y, int32_t w, int32_t h, int32_t color, bool filled)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_rect(&cmd, t.canvas, TX(t, x), TY(t, y), (uint16_t)w, (uint16_t)h,
                      (fmrb_color_t)color, filled);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_draw_rect(int32_t gfx, int32_t x, int32_t y, int32_t w, int32_t h, int32_t color)
{
    return rect(gfx, x, y, w, h, color, false);
}

int32_t fmrb_bnd_gfx_fill_rect(int32_t gfx, int32_t x, int32_t y, int32_t w, int32_t h, int32_t color)
{
    return rect(gfx, x, y, w, h, color, true);
}

int32_t fmrb_bnd_gfx_blend_rect(int32_t gfx, int32_t x, int32_t y, int32_t w, int32_t h, int32_t color,
                            int32_t mode)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_blend_rect(&cmd, t.canvas, TX(t, x), TY(t, y), (uint16_t)w, (uint16_t)h,
                            (fmrb_color_t)color, (uint8_t)mode);
    return submit(&t, &cmd);
}

static int32_t round_rect(int32_t gfx, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                          int32_t color, bool filled)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_round_rect(&cmd, t.canvas, TX(t, x), TY(t, y), (int16_t)w, (int16_t)h, (int16_t)r,
                            (fmrb_color_t)color, filled);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_draw_round_rect(int32_t gfx, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                                 int32_t color)
{
    return round_rect(gfx, x, y, w, h, r, color, false);
}

int32_t fmrb_bnd_gfx_fill_round_rect(int32_t gfx, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                                 int32_t color)
{
    return round_rect(gfx, x, y, w, h, r, color, true);
}

static int32_t circle(int32_t gfx, int32_t x, int32_t y, int32_t r, int32_t color, bool filled)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_circle(&cmd, t.canvas, TX(t, x), TY(t, y), (int16_t)r, (fmrb_color_t)color, filled);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_draw_circle(int32_t gfx, int32_t x, int32_t y, int32_t r, int32_t color)
{
    return circle(gfx, x, y, r, color, false);
}

int32_t fmrb_bnd_gfx_fill_circle(int32_t gfx, int32_t x, int32_t y, int32_t r, int32_t color)
{
    return circle(gfx, x, y, r, color, true);
}

static int32_t ellipse(int32_t gfx, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t color, bool filled)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_ellipse(&cmd, t.canvas, TX(t, x), TY(t, y), (int16_t)rx, (int16_t)ry,
                         (fmrb_color_t)color, filled);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_draw_ellipse(int32_t gfx, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t color)
{
    return ellipse(gfx, x, y, rx, ry, color, false);
}

int32_t fmrb_bnd_gfx_fill_ellipse(int32_t gfx, int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t color)
{
    return ellipse(gfx, x, y, rx, ry, color, true);
}

static int32_t triangle(int32_t gfx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                        int32_t color, bool filled)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_triangle(&cmd, t.canvas, TX(t, x0), TY(t, y0), TX(t, x1), TY(t, y1), TX(t, x2), TY(t, y2),
                          (fmrb_color_t)color, filled);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_draw_triangle(int32_t gfx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2,
                               int32_t y2, int32_t color)
{
    return triangle(gfx, x0, y0, x1, y1, x2, y2, color, false);
}

int32_t fmrb_bnd_gfx_fill_triangle(int32_t gfx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2,
                               int32_t y2, int32_t color)
{
    return triangle(gfx, x0, y0, x1, y1, x2, y2, color, true);
}

static int32_t arc(int32_t gfx, int32_t x, int32_t y, int32_t r0, int32_t r1, int32_t a0, int32_t a1,
                   int32_t color, bool filled)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_arc(&cmd, t.canvas, TX(t, x), TY(t, y), (int16_t)r0, (int16_t)r1, (int16_t)a0, (int16_t)a1,
                     (fmrb_color_t)color, filled);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_draw_arc(int32_t gfx, int32_t x, int32_t y, int32_t r0, int32_t r1, int32_t angle0,
                          int32_t angle1, int32_t color)
{
    return arc(gfx, x, y, r0, r1, angle0, angle1, color, false);
}

int32_t fmrb_bnd_gfx_fill_arc(int32_t gfx, int32_t x, int32_t y, int32_t r0, int32_t r1, int32_t angle0,
                          int32_t angle1, int32_t color)
{
    return arc(gfx, x, y, r0, r1, angle0, angle1, color, true);
}

/* ---- text ---------------------------------------------------------------- */

int32_t fmrb_bnd_gfx_draw_text_raw(int32_t gfx, int32_t x, int32_t y, const char *text, int32_t text_len,
                               int32_t color, int32_t bg_color, int32_t mixed)
{
    if (!text || text_len < 0) {
        return ERR_ARG;
    }
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_text_n(&cmd, t.canvas, TX(t, x), TY(t, y), text, (size_t)text_len,
                        (fmrb_color_t)color, (fmrb_color_t)(bg_color < 0 ? 0 : bg_color), bg_color < 0,
                        FMRB_FONT_SIZE_MEDIUM, mixed ? 1 : 0);
    return submit(&t, &cmd);
}

// family: FMRB_BND_GFX_SET_FONT_RAW_FAMILY_{DEFAULT,JA,JA_BOLD}. The sizes are the
// faces the display carries; picking the nearest one this machine has is
// the wrapper's (FmrbGfx#set_font). The display still falls back once more
// rather than draw nothing.
int32_t fmrb_bnd_gfx_set_font_raw(int32_t gfx, int32_t family, int32_t size)
{
    uint8_t fam;
    switch (family) {
    case FMRB_BND_GFX_SET_FONT_RAW_FAMILY_DEFAULT:
        fam = FMRB_LINK_GFX_FONT_FAMILY_DEFAULT;
        break;
    case FMRB_BND_GFX_SET_FONT_RAW_FAMILY_JA:
        fam = FMRB_LINK_GFX_FONT_FAMILY_JA;
        if (size != 8 && size != 12 && size != 16) {
            return ERR_ARG;
        }
        break;
    case FMRB_BND_GFX_SET_FONT_RAW_FAMILY_JA_BOLD:
        fam = FMRB_LINK_GFX_FONT_FAMILY_JA_BOLD;
        if (size != 12) {
            return ERR_ARG;
        }
        break;
    default:
        return ERR_ARG;
    }
#if defined(FMRB_NO_FONT_JA)
    // Built with FMRB_FONT_JA=0: misaki 8 is the only Japanese font.
    if (fam != FMRB_LINK_GFX_FONT_FAMILY_DEFAULT) {
        fam = FMRB_LINK_GFX_FONT_FAMILY_JA;
        size = 8;
    }
#endif
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_set_font(&cmd, t.canvas, fam, (uint8_t)size);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_set_text_size_raw(int32_t gfx, int32_t size)
{
    TARGET(gfx, t);
    if (size < 1) size = 1;
    if (size > 4) size = 4;
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_text_size(&cmd, t.canvas, (uint8_t)size);
    return submit(&t, &cmd);
}

// "MM/DD hh:mm:ss" formatted on the C stack and sent as one text command, so
// the desktop's 1Hz clock repaint allocates nothing. 0 (nothing drawn) while
// the wallclock is not set. A negative bg_color leaves the background as it
// is, as draw_text_raw's does.
int32_t fmrb_bnd_gfx_draw_wallclock(int32_t gfx, int32_t x, int32_t y, int32_t color, int32_t bg_color)
{
    TARGET(gfx, t);
    fmrb_wallclock_t wc;
    if (fmrb_hal_time_get_wallclock(&wc) != FMRB_OK) {
        return 0;
    }
    // 24 covers the compiler's worst case (each %02d may print 3 digits for
    // out-of-range field values); the normal output is 14 chars.
    char buf[24];
    snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02d:%02d",
             (int)wc.month, (int)wc.day, (int)wc.hour, (int)wc.minute, (int)wc.second);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_text(&cmd, t.canvas, TX(t, x), TY(t, y), buf, (fmrb_color_t)color,
                      (fmrb_color_t)(bg_color < 0 ? 0 : bg_color), bg_color < 0,
                      FMRB_FONT_SIZE_MEDIUM, 0);
    return submit(&t, &cmd) == 0 ? 1 : 0;
}

// The free internal RAM in KB as a fixed 5-char cell ("187KB"); "---KB" on
// the Linux builds, which have no meaningful figure. A negative bg_color
// leaves the background as it is.
int32_t fmrb_bnd_gfx_draw_free_iram(int32_t gfx, int32_t x, int32_t y, int32_t color, int32_t bg_color)
{
    TARGET(gfx, t);
    char buf[12];
#ifdef CONFIG_IDF_TARGET_LINUX
    snprintf(buf, sizeof(buf), "---KB");
#else
    unsigned kb = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    snprintf(buf, sizeof(buf), "%3uKB", kb > 999 ? 999u : kb);
#endif
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_text(&cmd, t.canvas, TX(t, x), TY(t, y), buf, (fmrb_color_t)color,
                      (fmrb_color_t)(bg_color < 0 ? 0 : bg_color), bg_color < 0,
                      FMRB_FONT_SIZE_MEDIUM, 0);
    return submit(&t, &cmd) == 0 ? 1 : 0;
}

/* ---- images and masks ---------------------------------------------------- */

// Sends CREATE_IMAGE_FROM_FILE straight through the transport (not host_task,
// which would deadlock waiting for its own process loop). 0 (nil) when the
// file could not be decoded: a broken image must not end the app.
int32_t fmrb_bnd_gfx_create_image(int32_t gfx, const char *path, int32_t path_len, fmrb_bnd_gfx_image_info_t *out)
{
    if (!path || path_len <= 0 || path_len >= GFX_PATH_MAX) {
        return ERR_ARG;
    }
    TARGET(gfx, t);
    uint8_t payload_buf[sizeof(fmrb_link_graphics_create_image_from_file_t) + GFX_PATH_MAX];
    size_t payload_len = sizeof(fmrb_link_graphics_create_image_from_file_t) + (size_t)path_len;
    fmrb_link_graphics_create_image_from_file_t *hdr =
        (fmrb_link_graphics_create_image_from_file_t *)payload_buf;
    hdr->canvas_id = (uint16_t)t.canvas;
    hdr->path_len = (uint16_t)path_len;
    memcpy(payload_buf + sizeof(*hdr), path, (size_t)path_len);

    uint8_t resp_buf[sizeof(fmrb_link_graphics_image_created_t)];
    uint32_t resp_len = sizeof(resp_buf);
    fmrb_err_t ret = fmrb_transport_send_sync(FMRB_LINK_TYPE_GRAPHICS, FMRB_LINK_GFX_CREATE_IMAGE_FROM_FILE,
                                              payload_buf, payload_len, resp_buf, &resp_len,
                                              10000);  // PNG decode
    if (ret != FMRB_OK || resp_len < sizeof(fmrb_link_graphics_image_created_t)) {
        return 0;
    }
    const fmrb_link_graphics_image_created_t *resp = (const fmrb_link_graphics_image_created_t *)resp_buf;
    out->id = resp->image_id;
    out->width = resp->width;
    out->height = resp->height;
    return 1;
}

// scale: 1.0 is the image's own size; a scale_y of 0 means "as scale_x". Sent
// as 8.8 fixed point.
int32_t fmrb_bnd_gfx_draw_image_raw(int32_t gfx, int32_t image_id, int32_t x, int32_t y, double scale_x,
                                double scale_y)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_draw_image(&cmd, t.canvas, (uint16_t)image_id, TX(t, x), TY(t, y), 0,
                            (int16_t)(scale_x * 256.0), (int16_t)(scale_y * 256.0));
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_delete_image(int32_t gfx, int32_t image_id)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_image(&cmd, t.canvas, (uint16_t)image_id);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_draw_tile_raw(int32_t gfx, int32_t image_id, int32_t src_x, int32_t src_y, int32_t w,
                               int32_t h, int32_t dst_x, int32_t dst_y)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_draw_tile(&cmd, t.canvas, (uint16_t)image_id, (int16_t)src_x, (int16_t)src_y, (uint16_t)w,
                           (uint16_t)h, TX(t, dst_x), TY(t, dst_y));
    return submit(&t, &cmd);
}

// scale 1..32 draws each pixel scale x scale; -2..-32 draws 1/|scale| of the
// size (0 and -1 are 1). The clip rect is from the origin like the rest; a
// clip_w or clip_h of 0 leaves only the canvas to clip.
int32_t fmrb_bnd_gfx_draw_tile_scaled_raw(int32_t gfx, int32_t image_id, int32_t src_x, int32_t src_y,
                                          int32_t w, int32_t h, int32_t dst_x, int32_t dst_y, int32_t scale,
                                          int32_t clip_x, int32_t clip_y, int32_t clip_w, int32_t clip_h)
{
    if (scale > FMRB_LINK_TILE_SCALE_MAX || scale < -FMRB_LINK_TILE_SCALE_MAX) {
        return ERR_ARG;
    }
    if (scale == 0 || scale == -1) {
        scale = 1;
    }
    TARGET(gfx, t);
    bool clipped = clip_w > 0 && clip_h > 0;
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_draw_tile_scaled(&cmd, t.canvas, (uint16_t)image_id, (int16_t)src_x, (int16_t)src_y,
                                  clamp_size(w), clamp_size(h), TX(t, dst_x), TY(t, dst_y), (int8_t)scale,
                                  clipped ? TX(t, clip_x) : 0, clipped ? TY(t, clip_y) : 0,
                                  clipped ? clamp_size(clip_w) : 0, clipped ? clamp_size(clip_h) : 0);
    return submit(&t, &cmd);
}

/* Mask upload: a BEGIN that reserves the buffer on the display, then the bits
   in chunks well below a frame (each its own frame, kept in order by the
   transport's mutex). */
#define MASK_CHUNK_OVERHEAD 18  // msgpack + COBS headroom (as file transfer)
#define MASK_CHUNK_SIZE (FMRB_LINK_FRAME_MAX_DATA - sizeof(fmrb_link_graphics_mask_data_t) - MASK_CHUNK_OVERHEAD)

int32_t fmrb_bnd_gfx_create_mask(int32_t gfx, int32_t width, int32_t height, const uint8_t *data, int32_t data_len)
{
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024 || !data) {
        return ERR_ARG;
    }
    size_t expected = (size_t)(((width + 7) / 8) * height);
    if (data_len < 0 || (size_t)data_len < expected) {
        return ERR_ARG;
    }
    TARGET(gfx, t);
    fmrb_link_graphics_create_mask_t begin = {
        .canvas_id = (uint16_t)t.canvas,
        .width = (uint16_t)width,
        .height = (uint16_t)height,
    };
    fmrb_link_graphics_mask_created_t resp = { .mask_id = 0 };
    uint32_t resp_len = sizeof(resp);
    fmrb_err_t ret = fmrb_transport_send_sync(FMRB_LINK_TYPE_GRAPHICS, FMRB_LINK_GFX_CREATE_MASK,
                                              (const uint8_t *)&begin, sizeof(begin),
                                              (uint8_t *)&resp, &resp_len, 5000);
    if (ret != FMRB_OK || resp_len < sizeof(resp) || resp.mask_id == 0) {
        return ERR_FAILED;
    }
    uint16_t mask_id = resp.mask_id;

    uint8_t chunk_buf[FMRB_LINK_FRAME_MAX_DATA];
    size_t offset = 0;
    while (offset < expected) {
        size_t remaining = expected - offset;
        size_t chunk_len = (remaining > MASK_CHUNK_SIZE) ? MASK_CHUNK_SIZE : remaining;
        fmrb_link_graphics_mask_data_t *hdr = (fmrb_link_graphics_mask_data_t *)chunk_buf;
        hdr->mask_id = mask_id;
        hdr->offset = (uint32_t)offset;
        hdr->chunk_len = (uint16_t)chunk_len;
        memcpy(chunk_buf + sizeof(*hdr), data + offset, chunk_len);
        // A failed chunk leaves the slot to the app's exit or delete_mask.
        if (fmrb_transport_send(FMRB_LINK_TYPE_GRAPHICS, FMRB_LINK_GFX_MASK_DATA, chunk_buf,
                                sizeof(*hdr) + chunk_len, FMRB_TRANSPORT_TIMEOUT_DEFAULT) != FMRB_OK) {
            return ERR_FAILED;
        }
        offset += chunk_len;
    }
    return (int32_t)mask_id;
}

// Through the host queue, so it stays ordered after the draw_image_masked
// calls that may still use the mask.
int32_t fmrb_bnd_gfx_delete_mask(int32_t gfx, int32_t mask_id)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_mask(&cmd, t.canvas, (uint16_t)mask_id);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_draw_image_masked_raw(int32_t gfx, int32_t image_id, int32_t mask_id, int32_t x, int32_t y)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_draw_image_masked(&cmd, t.canvas, (uint16_t)image_id, (uint16_t)mask_id, TX(t, x), TY(t, y));
    return submit(&t, &cmd);
}

/* ---- present and the canvas-wide settings -------------------------------- */

// Not positioned: where the app's window is. Positioned: (x, y) on the
// screen (an extra canvas of FmrbApp#create_canvas_gfx, which the window
// manager does not track).
/* The window frame's outline, drawn again just before the window is shown.
   The bottom corners' arcs (with CORNER_R 4: (1,h-3), (1,h-2), (2,h-2) and
   their mirror) lie inside the user area, which is a plain rectangle
   (1, 11, w-2, h-12): anything the app draws at the bottom of its area --
   a clear with fill_rect, a background -- wipes them, and the border then
   stopped short of both bottom corners. Every engine presents through here
   (the mruby gem's present, Spinel's present_raw, the C shell's apps through
   the boundary), so one outline here keeps every framed window whole
   whatever its app drew. The same outline as the frame's (fmrb-app.rb,
   fmrb_app_base_spinel.rb, fmrb_app_shell.c): one command, the border's
   pixels only. */
#define FRAME_CORNER_R 4

static void restamp_frame_outline(const gfx_target_t *t, int32_t gfx)
{
    const fmrb_app_task_context_t *ctx = t->ctx;
    if (!ctx || ctx->canvas_id == 0 || (uint16_t)t->canvas != ctx->canvas_id) return;
    if (ctx->fullscreen || ctx->frameless || ctx->bg_canvas_id != 0 || ctx->headless) return;
    if (ctx->window_width < 2 * FRAME_CORNER_R || ctx->window_height < 2 * FRAME_CORNER_R) return;
    const fmrb_gfx_drawer_t *d = drawer_in(t->ctx, gfx);
    if (!d || d->to_image) return;
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_round_rect(&cmd, t->canvas, 0, 0, (int16_t)ctx->window_width,
                            (int16_t)ctx->window_height, FRAME_CORNER_R,
                            fmrb_theme_get()->border, false);
    submit(t, &cmd);
}

int32_t fmrb_bnd_gfx_present_raw(int32_t gfx, int32_t x, int32_t y, int32_t positioned)
{
    TARGET(gfx, t);  // (a drawer was found: t.ctx is the app's context)
    restamp_frame_outline(&t, gfx);
    gfx_cmd_t cmd;  // 0xFF = no transparency
    fmrb_gfx_cmd_present(&cmd, t.canvas, positioned ? (int16_t)x : (int16_t)t.ctx->window_pos_x,
                         positioned ? (int16_t)y : (int16_t)t.ctx->window_pos_y, 0xFF);
    return submit(&t, &cmd);
}

// The composite source viewport (the hardware scroll register; Modern only,
// the Retro display ignores it). A width of 0 clears it.
int32_t fmrb_bnd_gfx_set_viewport(int32_t gfx, int32_t src_x, int32_t src_y, int32_t w, int32_t h)
{
    TARGET(gfx, t);
    fmrb_gfx_context_t gctx = fmrb_gfx_get_global_context();
    if (!gctx) {
        return ERR_FAILED;
    }
    fmrb_gfx_set_canvas_viewport(gctx, t.canvas, (uint16_t)src_x, (uint16_t)src_y, (uint16_t)w, (uint16_t)h);
    return 0;
}

int32_t fmrb_bnd_gfx_clear_viewport(int32_t gfx)
{
    return fmrb_bnd_gfx_set_viewport(gfx, 0, 0, 0, 0);
}

// Best effort, as before: a list the display refuses is logged there.
int32_t fmrb_bnd_gfx_set_composite_regions_raw(int32_t gfx, const fmrb_bnd_gfx_composite_region_t *regions,
                                           int32_t regions_count)
{
    if (regions_count < 0 || regions_count > FMRB_GFX_MAX_COMPOSITE_REGIONS ||
        (regions_count > 0 && !regions)) {
        return ERR_ARG;
    }
    TARGET(gfx, t);
    fmrb_gfx_context_t gctx = fmrb_gfx_get_global_context();
    if (!gctx) {
        return ERR_FAILED;
    }
    fmrb_gfx_composite_region_t list[FMRB_GFX_MAX_COMPOSITE_REGIONS];
    for (int32_t i = 0; i < regions_count; i++) {
        list[i].src_x = regions[i].src_x;
        list[i].src_y = regions[i].src_y;
        list[i].dst_x = regions[i].dst_x;
        list[i].dst_y = regions[i].dst_y;
        list[i].w = regions[i].w;
        list[i].h = regions[i].h;
        list[i].use_transparent = regions[i].transparent ? 1 : 0;
    }
    fmrb_gfx_set_composite_regions(gctx, t.canvas, list, (uint8_t)regions_count);
    return 0;
}

// The rect is in sprite coordinates, which are the area's (from the origin)
// whatever the drawer draws into: a sprite instance is placed from the
// origin (SpriteInstance#move) also while the drawer draws into a sprite
// image, so the clip is too. A width of 0 clears the clip.
int32_t fmrb_bnd_gfx_set_sprite_clip(int32_t gfx, int32_t x, int32_t y, int32_t w, int32_t h)
{
    TARGET(gfx, t);
    fmrb_gfx_context_t gctx = fmrb_gfx_get_global_context();
    if (!gctx) {
        return ERR_FAILED;
    }
    if (w > 0) {
        x += t.ax;
        y += t.ay;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    fmrb_gfx_set_sprite_clip(gctx, t.canvas, (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h);
    return 0;
}

int32_t fmrb_bnd_gfx_clear_sprite_clip(int32_t gfx)
{
    return fmrb_bnd_gfx_set_sprite_clip(gfx, 0, 0, 0, 0);
}

int32_t fmrb_bnd_gfx_set_output_level(int32_t gfx, int32_t level)
{
    TARGET(gfx, t);
    if (level < 0) level = 0;
    if (level > 255) level = 255;
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_set_output_level(&cmd, t.canvas, (uint8_t)level);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_set_chroma_level(int32_t gfx, int32_t level)
{
    TARGET(gfx, t);
    if (level < 0) level = 0;
    if (level > 255) level = 255;
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_set_chroma_level(&cmd, t.canvas, (uint8_t)level);
    return submit(&t, &cmd);
}

/* ---- files on the graphics side ------------------------------------------ */

/* A file_cmd_t to host_task, waiting for its result (*result is the caller's
   stack and host_task writes through it, so a forced delete is held off). */
static fmrb_err_t file_cmd_sync(file_cmd_t *cmd, file_cmd_result_t *result, uint32_t timeout_ms)
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
    fmrb_app_sync_io_begin();
    fmrb_base_type_t sem_ret = fmrb_semaphore_take(result->done_sem, FMRB_MS_TO_TICKS(timeout_ms));
    fmrb_app_sync_io_end();
    fmrb_semaphore_delete(result->done_sem);
    return (sem_ret == FMRB_PASS) ? FMRB_OK : FMRB_ERR_TIMEOUT;
}

// Transfers only when the graphics-side copy differs (size + CRC32). 1 when
// the two match afterwards.
int32_t fmrb_bnd_gfx_sync_file_raw(int32_t gfx, const char *path, int32_t path_len, const char *dest,
                               int32_t dest_len)
{
    (void)gfx;
    char src[256];
    char dst[GFX_PATH_MAX];
    if (!copy_path(src, sizeof(src), path, path_len) || !copy_path(dst, sizeof(dst), dest, dest_len)) {
        return 0;  // as before: a path that cannot be sent is "not in sync"
    }
    return fmrb_kernel_sync_file(src, dst) == FMRB_OK ? 1 : 0;
}

// Reads the local file whole and hands it to host_task, which owns the buffer
// from then on.
int32_t fmrb_bnd_gfx_transfer_file_raw(int32_t gfx, const char *path, int32_t path_len, const char *dest,
                                   int32_t dest_len)
{
    (void)gfx;
    char src[256];
    char dst[GFX_PATH_MAX];
    if (!copy_path(src, sizeof(src), path, path_len) || !copy_path(dst, sizeof(dst), dest, dest_len)) {
        return ERR_ARG;
    }
    fmrb_file_info_t info;
    if (fmrb_hal_file_stat(src, &info) != FMRB_OK || info.size == 0) {
        return ERR_FAILED;
    }
    uint32_t file_size = (uint32_t)info.size;
    uint8_t *file_data = (uint8_t *)fmrb_sys_malloc(file_size);
    if (!file_data) {
        return ERR_FAILED;
    }
    fmrb_file_t fh;
    if (fmrb_hal_file_open(src, FMRB_O_RDONLY, &fh) != FMRB_OK) {
        fmrb_sys_free(file_data);
        return ERR_FAILED;
    }
    size_t bytes_read = 0;
    fmrb_err_t rret = fmrb_hal_file_read(fh, file_data, file_size, &bytes_read);
    fmrb_hal_file_close(fh);
    if (rret != FMRB_OK || bytes_read != file_size) {
        fmrb_sys_free(file_data);
        return ERR_FAILED;
    }

    file_cmd_t cmd = {0};
    file_cmd_result_t result = {0};
    cmd.cmd_type = FILE_CMD_TRANSFER;
    cmd.path_len = (uint16_t)dest_len;
    memcpy(cmd.path, dst, (size_t)dest_len);
    cmd.params.transfer.data = file_data;
    cmd.params.transfer.data_len = file_size;
    if (file_cmd_sync(&cmd, &result, 30000) != FMRB_OK) {
        fmrb_sys_free(file_data);  // not sent: still ours
        return ERR_FAILED;
    }
    return result.result == 0 ? 1 : ERR_FAILED;
}

// Through the host queue (a direct transport call from host_task's own loop
// would deadlock). A file that is not there, or no answer, is exists = 0.
int32_t fmrb_bnd_gfx_file_status(int32_t gfx, const char *path, int32_t path_len, fmrb_bnd_gfx_file_info_t *out)
{
    (void)gfx;
    if (!path || path_len <= 0 || path_len >= GFX_PATH_MAX) {
        return ERR_ARG;
    }
    out->exists = 0;
    out->size = 0;

    file_cmd_result_t result;
    result.done_sem = fmrb_semaphore_create_binary();
    if (!result.done_sem) {
        return ERR_FAILED;
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
    cmd->path_len = (uint16_t)path_len;
    memcpy(cmd->path, path, (size_t)path_len);

    if (fmrb_msg_send(PROC_ID_HOST, &msg, 5000) != FMRB_OK) {
        fmrb_semaphore_delete(result.done_sem);
        return ERR_FAILED;
    }
    fmrb_app_sync_io_begin();
    fmrb_base_type_t wait_ret = fmrb_semaphore_take(result.done_sem, FMRB_MS_TO_TICKS(5000));
    fmrb_app_sync_io_end();
    fmrb_semaphore_delete(result.done_sem);
    if (wait_ret == FMRB_TRUE && result.result == 0 && result.data.status.exists) {
        out->exists = 1;
        out->size = (uint32_t)result.data.status.file_size;
    }
    return 1;
}

// The app's own file, its head only (the same C as BMP332.info). 0 (nil)
// when it is not there or is not a picture BMP332 reads.
int32_t fmrb_bnd_gfx_picture_info(int32_t gfx, const char *path, int32_t path_len,
                                  fmrb_bnd_gfx_picture_info_t *out)
{
    (void)gfx;
    char p[256];
    if (!copy_path(p, sizeof(p), path, path_len)) {
        return 0;
    }
    fmrb_picture332_file_info_t info;
    if (fmrb_picture332_file_info(p, &info) != 0) {
        return 0;
    }
    out->width = info.width;
    out->height = info.height;
    out->format = info.format;
    out->bytes = info.bytes;
    out->mtime = info.mtime;
    return 1;
}

/* ---- sprites ------------------------------------------------------------- */

// An image and a placed sprite are the graphics side's, numbered within the
// drawer's canvas. A sprite's position is from the origin, also while the
// drawer draws into a sprite image (a sprite is placed on the canvas); what
// is drawn into an image is in the image's own coordinates (to_image).

int32_t fmrb_bnd_gfx_create_sprite_image(int32_t gfx, int32_t width, int32_t height,
                                         int32_t transparent_color, int32_t use_transparent)
{
    if (width <= 0 || height <= 0 || width > 0xFFFF || height > 0xFFFF) {
        return ERR_ARG;
    }
    TARGET(gfx, t);
    fmrb_gfx_context_t gctx = fmrb_gfx_get_global_context();
    if (!gctx) {
        return ERR_FAILED;
    }
    uint16_t id = fmrb_gfx_create_sprite_image(gctx, t.canvas, (uint16_t)width, (uint16_t)height,
                                               (uint8_t)transparent_color, use_transparent != 0);
    return id == 0 ? ERR_FAILED : (int32_t)id;
}

int32_t fmrb_bnd_gfx_delete_sprite_image(int32_t gfx, int32_t image_id)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_sprite_image(&cmd, t.canvas, (uint16_t)image_id);
    return submit(&t, &cmd);
}

// A PNG or an 8-bit RGB332 BMP, told apart by the graphics side (which
// decodes it) from the first bytes (doc/flash_layout F2).
int32_t fmrb_bnd_gfx_load_sprite_image(int32_t gfx, int32_t image_id, const char *path, int32_t path_len)
{
    if (!path || path_len <= 0 || path_len >= GFX_PATH_MAX) {
        return ERR_ARG;
    }
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_load_sprite_image_bmp_n(&cmd, t.canvas, (uint16_t)image_id, path, (size_t)path_len);
    return submit(&t, &cmd);
}

// The pixels go through the host queue in pieces of up to
// FMRB_GFX_SPRITE_DATA_CHUNK bytes (whole rows when a row fits, else pieces
// of one row), so they stay in order with the draw_tile calls around them:
// rewriting an image that is still being drawn from cannot change a stamp
// queued before. The rect is the image's own (no origin).
#define SPRITE_DATA_MAX_PIXELS 65536

int32_t fmrb_bnd_gfx_write_sprite_image(int32_t gfx, int32_t image_id, int32_t x, int32_t y, int32_t w,
                                        int32_t h, const uint8_t *pixels, int32_t pixels_len)
{
    if (w <= 0 || h <= 0 || w > 0xFFFF || h > 0xFFFF || !pixels) {
        return ERR_ARG;
    }
    int64_t n = (int64_t)w * h;
    if (n > SPRITE_DATA_MAX_PIXELS || pixels_len < n) {
        return ERR_ARG;
    }
    if (x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y + h - 1 > INT16_MAX) {
        return ERR_ARG;
    }
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    const int32_t chunk = FMRB_GFX_SPRITE_DATA_CHUNK;
    if (w <= chunk) {
        int32_t rows = chunk / w;
        for (int32_t r = 0; r < h; r += rows) {
            int32_t nr = (h - r < rows) ? h - r : rows;
            fmrb_gfx_cmd_sprite_image_data(&cmd, t.canvas, (uint16_t)image_id, (int16_t)x, (int16_t)(y + r),
                                           (uint16_t)w, (uint16_t)nr, pixels + (size_t)r * (size_t)w);
            if (submit(&t, &cmd) != 0) return ERR_FAILED;
        }
        return 0;
    }
    for (int32_t r = 0; r < h; r++) {
        for (int32_t c = 0; c < w; c += chunk) {
            int32_t nc = (w - c < chunk) ? w - c : chunk;
            if (x + c > INT16_MAX) break;
            fmrb_gfx_cmd_sprite_image_data(&cmd, t.canvas, (uint16_t)image_id, (int16_t)(x + c),
                                           (int16_t)(y + r), (uint16_t)nc, 1,
                                           pixels + (size_t)r * (size_t)w + (size_t)c);
            if (submit(&t, &cmd) != 0) return ERR_FAILED;
        }
    }
    return 0;
}

// Through the host_task queue, so the switch stays in order with the drawing
// around it (bypassing it lets target 0 race past the tail of a sprite draw
// and clips its bottom-right pixels). The drawer adds no origin while it
// draws into the image.
int32_t fmrb_bnd_gfx_set_sprite_image_target(int32_t gfx, int32_t image_id)
{
    TARGET(gfx, t);
    fmrb_gfx_drawer_set_to_image(gfx, image_id != 0);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_set_sprite_image_target(&cmd, t.canvas, (uint16_t)image_id);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_create_sprite_instance(int32_t gfx, const uint16_t *image_ids, int32_t image_ids_count,
                                            int32_t x, int32_t y, int32_t z)
{
    if (!image_ids || image_ids_count <= 0 || image_ids_count > FMRB_SPRITE_MAX_FRAMES) {
        return ERR_ARG;
    }
    TARGET(gfx, t);
    fmrb_gfx_context_t gctx = fmrb_gfx_get_global_context();
    if (!gctx) {
        return ERR_FAILED;
    }
    uint16_t id = fmrb_gfx_create_sprite_instance(gctx, t.canvas, image_ids, (uint8_t)image_ids_count,
                                                  (int16_t)(x + t.ax), (int16_t)(y + t.ay), (int16_t)z);
    return id == 0 ? ERR_FAILED : (int32_t)id;
}

int32_t fmrb_bnd_gfx_delete_sprite_instance(int32_t gfx, int32_t instance_id)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_sprite_instance(&cmd, t.canvas, (uint16_t)instance_id);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_move_sprite(int32_t gfx, int32_t instance_id, int32_t x, int32_t y)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_sprite_instance_move(&cmd, t.canvas, (uint16_t)instance_id, (int16_t)(x + t.ax),
                                      (int16_t)(y + t.ay));
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_set_sprite_visible(int32_t gfx, int32_t instance_id, int32_t visible)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_sprite_instance_set_visible(&cmd, t.canvas, (uint16_t)instance_id, visible != 0);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_set_sprite_frame(int32_t gfx, int32_t instance_id, int32_t frame)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_sprite_instance_set_frame(&cmd, t.canvas, (uint16_t)instance_id, (uint8_t)frame);
    return submit(&t, &cmd);
}

int32_t fmrb_bnd_gfx_delete_all_sprites(int32_t gfx)
{
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_delete_all_sprites(&cmd, t.canvas);
    return submit(&t, &cmd);
}

/* ---- video and the picture on screen -------------------------------------- */

// Motion-JPEG playback is the display task's: the P4 (its hardware decoder)
// and the hosted builds (the native one and the browser, a software decoder,
// hosted/backend/display_video_soft.c). Retro has no decoder and its display
// cannot read this board's files, and the Linux simulator's display is
// another process that knows none of these commands (every call would cost a
// full sync timeout): there the answer is "none" at once, so the app can
// fall back. This is not FMRB_HW_FAMILY_MODERN, which takes the simulator in.
#if defined(FMRB_HW_MODERN) || defined(FMRB_PLATFORM_HOSTED)
#define GFX_HAS_VIDEO 1
#else
#define GFX_HAS_VIDEO 0
#endif

int32_t fmrb_bnd_gfx_video_open_raw(int32_t gfx, const char *path, int32_t path_len, int32_t x, int32_t y,
                                    int32_t fps, int32_t loop, fmrb_bnd_gfx_video_info_t *out)
{
    if (!path || path_len <= 0 || path_len >= GFX_PATH_MAX) {
        return ERR_ARG;
    }
    TARGET(gfx, t);
#if !GFX_HAS_VIDEO
    (void)t; (void)x; (void)y; (void)fps; (void)loop; (void)out;
    return 0;
#else
    uint8_t payload_buf[sizeof(fmrb_link_graphics_video_open_t) + GFX_PATH_MAX];
    size_t payload_len = sizeof(fmrb_link_graphics_video_open_t) + (size_t)path_len;
    fmrb_link_graphics_video_open_t *hdr = (fmrb_link_graphics_video_open_t *)payload_buf;
    hdr->canvas_id = (uint16_t)t.canvas;
    hdr->x = TX(t, x);
    hdr->y = TY(t, y);
    hdr->fps = (uint16_t)fps;
    hdr->flags = loop ? FMRB_LINK_GFX_VIDEO_FLAG_LOOP : 0;
    hdr->path_len = (uint16_t)path_len;
    memcpy(payload_buf + sizeof(*hdr), path, (size_t)path_len);

    uint8_t resp_buf[sizeof(fmrb_link_graphics_video_opened_t)];
    uint32_t resp_len = sizeof(resp_buf);
    fmrb_err_t ret = fmrb_transport_send_sync(FMRB_LINK_TYPE_GRAPHICS, FMRB_LINK_GFX_VIDEO_OPEN, payload_buf,
                                              payload_len, resp_buf, &resp_len, 10000);
    if (ret != FMRB_OK || resp_len < sizeof(fmrb_link_graphics_video_opened_t)) {
        return 0;
    }
    const fmrb_link_graphics_video_opened_t *resp = (const fmrb_link_graphics_video_opened_t *)resp_buf;
    if (resp->result != 0) {
        return 0;   // the app can fall back
    }
    out->width = resp->width;
    out->height = resp->height;
    return 1;
#endif
}

#if GFX_HAS_VIDEO
static int32_t video_ask(uint8_t cmd, const uint8_t *payload, size_t payload_len, fmrb_bnd_gfx_video_status_t *out)
{
    uint8_t resp_buf[sizeof(fmrb_link_graphics_video_status_t)];
    uint32_t resp_len = sizeof(resp_buf);
    fmrb_err_t ret = fmrb_transport_send_sync(FMRB_LINK_TYPE_GRAPHICS, cmd, payload, payload_len, resp_buf,
                                              &resp_len, 5000);
    if (ret != FMRB_OK || resp_len < sizeof(fmrb_link_graphics_video_status_t)) {
        return 0;
    }
    const fmrb_link_graphics_video_status_t *st = (const fmrb_link_graphics_video_status_t *)resp_buf;
    out->state = st->state;
    out->shown = st->frames_shown;
    out->dropped = st->frames_dropped;
    return 1;
}
#endif

// action: FMRB_BND_GFX_VIDEO_CONTROL_ACTION_PLAY (0), PAUSE, STOP, REWIND,
// the protocol's numbers.
int32_t fmrb_bnd_gfx_video_control(int32_t action, fmrb_bnd_gfx_video_status_t *out)
{
    if (action < 0 || action > 3) {
        return ERR_ARG;
    }
#if !GFX_HAS_VIDEO
    (void)out;
    return 0;
#else
    fmrb_link_graphics_video_control_t payload;
    payload.action = (uint8_t)action;
    return video_ask(FMRB_LINK_GFX_VIDEO_CONTROL, (const uint8_t *)&payload, sizeof(payload), out);
#endif
}

int32_t fmrb_bnd_gfx_video_status(fmrb_bnd_gfx_video_status_t *out)
{
#if !GFX_HAS_VIDEO
    (void)out;
    return 0;
#else
    return video_ask(FMRB_LINK_GFX_VIDEO_STATUS, NULL, 0, out);
#endif
}

// Queued behind what was sent before, so the picture written is the one the
// last present composited. Nothing waits: Tab5 writes a JPEG into the
// filesystem it shares with this side (File.exist? tells when it is done),
// the simulator a BMP into its own storage; Retro says in its log that it
// cannot.
int32_t fmrb_bnd_gfx_export_frame(int32_t gfx, const char *path, int32_t path_len)
{
    if (!path || path_len <= 0 || path_len >= GFX_PATH_MAX) {
        return ERR_ARG;
    }
    TARGET(gfx, t);
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_export_frame_n(&cmd, t.canvas, path, (size_t)path_len);
    return submit(&t, &cmd);
}

/* ---- colour -------------------------------------------------------------- */

int32_t fmrb_bnd_gfx_rgb_to_332(int32_t r, int32_t g, int32_t b)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
}
