/*
 * The app shell (fmrb_app_shell.h, doc/app_boundary W3): the window, its
 * frame and the app's life in C, for an engine that only exports functions.
 *
 * Everything here mirrors the Ruby base class FmrbApp
 * (lib/gems/picoruby-fmrb-app/mrblib/fmrb-app.rb and ports/esp32/app.c):
 * _layout_user_area, draw_window_frame (its two GfxBlocks drawn one command
 * at a time, as the Python base does), _apply_rounded_corner_regions,
 * _frame_event, _handle_system_control, main_loop / _spin, stop and destroy.
 * The picture and the behaviour are meant to be the same, so a change to one
 * of them belongs in both (the list is in doc/app_boundary/report/w3.md).
 */
#include "fmrb_app_shell.h"

#include <stdio.h>
#include <string.h>
#include "fmrb_app.h"
#include "fmrb_app_canvas.h"
#include "fmrb_gfx.h"
#include "fmrb_gfx_cmd.h"
#include "fmrb_hid_decode.h"
#include "fmrb_hal_time.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"
#include "fmrb_rtos.h"
#include "fmrb_theme.h"
#include "fmrb_task_config.h"

static const char *TAG = "app_shell";

/* The frame's measures (FmrbApp's constants). */
#define TITLE_BAR_H            11
#define CORNER_R               4
#define RESIZE_MARK            8
#define TRANSPARENT_COLOR      0x01
#define CLOSE_BTN_CX_OFFSET    6
#define CLOSE_BTN_CY           5
#define CLOSE_BTN_R            3
#define CLOSE_BTN_HIT_R        5
#define CLOSE_BTN_PRESSED_COLOR 0x49
#define SUSPENDED_WAIT_MS      500

typedef struct {
    fmrb_app_task_context_t   *ctx;
    const fmrb_shell_engine_t *eng;
    void                      *ud;

    int       canvas;            /* -1 headless */
    bool      bg;                /* the desktop's wallpaper layer (no frame) */
    bool      fullscreen;
    bool      frameless;
    bool      rounded;
    bool      resizable;
    uint16_t  ww, wh;            /* window */
    int16_t   ax, ay;            /* origin of the area */
    uint16_t  aw, ah;            /* area */
    uint16_t  region_w, region_h;/* composite regions last declared for */

    bool      running;
    bool      raised;            /* an app call raised: the app ends as an error */
    bool      suspended;
    bool      close_pressed;

    bool      in_call;
    fmrb_time_t call_started_us;
} shell_t;

static shell_t *shell_of(fmrb_app_task_context_t *ctx)
{
    return ctx ? (shell_t *)ctx->shell : NULL;
}

/* ---- calls into the app ------------------------------------------------- */

static void call_begin(shell_t *s)
{
    s->in_call = true;
    s->call_started_us = fmrb_hal_time_get_us();
}

/* Whatever came back: CLOSED ends the app as asked, RAISED as an error. */
static int call_end(shell_t *s, int rc)
{
    s->in_call = false;
    if (rc == FMRB_SHELL_CLOSED) {
        s->running = false;
        s->ctx->expected_stop = true;
    } else if (rc != FMRB_SHELL_OK) {
        s->running = false;
        s->raised = true;
    }
    return rc;
}

static int call_hook(shell_t *s, fmrb_shell_hook_t hook)
{
    if (hook != FMRB_SHELL_HOOK_LOAD && !s->eng->has_hook(s->ud, hook)) {
        return FMRB_SHELL_OK;
    }
    call_begin(s);
    return call_end(s, s->eng->call(s->ud, hook));
}

/* ---- drawing ------------------------------------------------------------ */

static void submit(gfx_cmd_t *cmd)
{
    fmrb_err_t ret = fmrb_gfx_submit(cmd);
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "gfx submit failed: %d", ret);
    }
}

static void fill_rect(shell_t *s, int x, int y, int w, int h, int c)
{
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_rect(&cmd, (fmrb_canvas_handle_t)s->canvas, (int16_t)x, (int16_t)y,
                      (uint16_t)w, (uint16_t)h, (fmrb_color_t)c, true);
    submit(&cmd);
}

static void line(shell_t *s, int x0, int y0, int x1, int y1, int c)
{
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_line(&cmd, (fmrb_canvas_handle_t)s->canvas, (int16_t)x0, (int16_t)y0,
                      (int16_t)x1, (int16_t)y1, (fmrb_color_t)c);
    submit(&cmd);
}

static void circle(shell_t *s, int x, int y, int r, int c)
{
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_circle(&cmd, (fmrb_canvas_handle_t)s->canvas, (int16_t)x, (int16_t)y,
                        (int16_t)r, (fmrb_color_t)c, true);
    submit(&cmd);
}

static void present(shell_t *s)
{
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_present(&cmd, (fmrb_canvas_handle_t)s->canvas,
                         (int16_t)s->ctx->window_pos_x, (int16_t)s->ctx->window_pos_y,
                         0xFF);
    submit(&cmd);
}

/* _apply_rounded_corner_regions: per-pixel colour-key compare on the four
   corner squares only, the rest of the canvas on the opaque fast path; the
   whole window keyed for a frameless one. Declared again only when the size
   changed. */
static void apply_regions(shell_t *s)
{
    if (s->fullscreen || s->canvas < 0 || s->bg) return;
    if (!s->rounded && !s->frameless) return;
    if (s->region_w == s->ww && s->region_h == s->wh) return;
    int16_t w = (int16_t)s->ww, h = (int16_t)s->wh, c = CORNER_R;
    fmrb_gfx_composite_region_t r[7];
    uint8_t n;
    if (s->frameless) {
        r[0] = (fmrb_gfx_composite_region_t){0, 0, 0, 0, w, h, 1};
        n = 1;
    } else {
        r[0] = (fmrb_gfx_composite_region_t){0,     0,     0,     0,     c, c, 1};
        r[1] = (fmrb_gfx_composite_region_t){w - c, 0,     w - c, 0,     c, c, 1};
        r[2] = (fmrb_gfx_composite_region_t){0,     h - c, 0,     h - c, c, c, 1};
        r[3] = (fmrb_gfx_composite_region_t){w - c, h - c, w - c, h - c, c, c, 1};
        r[4] = (fmrb_gfx_composite_region_t){c, 0,     c, 0,     (int16_t)(w - 2 * c), c, 0};
        r[5] = (fmrb_gfx_composite_region_t){c, h - c, c, h - c, (int16_t)(w - 2 * c), c, 0};
        r[6] = (fmrb_gfx_composite_region_t){0, c,     0, c,     w, (int16_t)(h - 2 * c), 0};
        n = 7;
    }
    fmrb_gfx_set_composite_regions(fmrb_gfx_get_global_context(),
                                   (fmrb_canvas_handle_t)s->canvas, r, n);
    s->region_w = s->ww;
    s->region_h = s->wh;
}

/* draw_window_frame: the resize mark, the frame block, the corner-clear
   block, the composite regions -- in that order, as the Ruby base. */
static void draw_frame(shell_t *s)
{
    if (s->canvas < 0 || s->fullscreen || s->bg) return;
    if (s->frameless) {
        apply_regions(s);
        return;
    }
    const fmrb_theme_t *t = fmrb_theme_get();
    int w = s->ww, h = s->wh;
    gfx_cmd_t cmd;
    fmrb_canvas_handle_t cv = (fmrb_canvas_handle_t)s->canvas;

    if (s->resizable) {
        int x = w - 2, y = h - 2, m = RESIZE_MARK - 1;
        fmrb_gfx_cmd_triangle(&cmd, cv, (int16_t)x, (int16_t)(y - m), (int16_t)x, (int16_t)y,
                              (int16_t)(x - m), (int16_t)y, t->menu_bg, true);
        submit(&cmd);
    }

    /* Title bar with rounded top corners, its bottom edge squared off. */
    fmrb_gfx_cmd_round_rect(&cmd, cv, 0, 0, (int16_t)w, TITLE_BAR_H, CORNER_R, t->menu_bg, true);
    submit(&cmd);
    fill_rect(s, 0, CORNER_R, w, TITLE_BAR_H - CORNER_R, t->menu_bg);
    /* Menu mark (three bars) and the title. */
    fill_rect(s, 3, 3, 9, 1, t->text_light);
    fill_rect(s, 3, 5, 9, 1, t->text_light);
    fill_rect(s, 3, 7, 9, 1, t->text_light);
    fmrb_gfx_cmd_text(&cmd, cv, 15, 2, s->ctx->app_name, t->text_light, 0, true,
                      FMRB_FONT_SIZE_MEDIUM, 0);
    submit(&cmd);
    /* Close button. */
    circle(s, w - CLOSE_BTN_CX_OFFSET, CLOSE_BTN_CY, CLOSE_BTN_R, t->text_light);
    /* Rounded border. */
    fmrb_gfx_cmd_round_rect(&cmd, cv, 0, 0, (int16_t)w, (int16_t)h, CORNER_R, t->border, false);
    submit(&cmd);

    /* The three pixels outside each corner arc, back to the colour key. */
    int k = TRANSPARENT_COLOR;
    line(s, 0, 0, 1, 0, k);
    line(s, 0, 1, 0, 1, k);
    line(s, w - 2, 0, w - 1, 0, k);
    line(s, w - 1, 1, w - 1, 1, k);
    line(s, 0, h - 2, 0, h - 1, k);
    line(s, 1, h - 1, 1, h - 1, k);
    line(s, w - 1, h - 2, w - 1, h - 1, k);
    line(s, w - 2, h - 1, w - 2, h - 1, k);

    apply_regions(s);
}

static void clear_area(shell_t *s, int color)
{
    if (s->canvas < 0) return;
    if (color < 0) {
        color = s->frameless ? TRANSPARENT_COLOR : fmrb_theme_get()->window_bg;
    }
    fill_rect(s, s->ax, s->ay, s->aw, s->ah, color);
    draw_frame(s);
}

/* ---- layout ------------------------------------------------------------- */

/* _layout_user_area: a fullscreen or frameless window owns all of it, and so
   does the desktop's; a framed one loses the border and the title bar. */
static void layout(shell_t *s)
{
    if (s->fullscreen || s->frameless || s->bg) {
        s->ax = 0;
        s->ay = 0;
        s->aw = s->ww;
        s->ah = s->wh;
    } else {
        s->ax = 1;
        s->ay = TITLE_BAR_H;
        s->aw = (uint16_t)(s->ww - 2);
        s->ah = (uint16_t)(s->wh - TITLE_BAR_H - 1);
    }
    fmrb_shell_layout_t l = {
        .canvas = s->canvas,
        .window_w = s->ww, .window_h = s->wh,
        .area_x = s->ax, .area_y = s->ay,
        .area_w = s->aw, .area_h = s->ah,
        .fullscreen = s->fullscreen,
    };
    s->eng->layout(s->ud, &l);
}

/* ---- messages to the kernel --------------------------------------------- */

static size_t mp_str(uint8_t *p, const char *str)
{
    size_t n = strlen(str);
    p[0] = (uint8_t)(0xA0 | n);   /* fixstr: every key here is < 32 bytes */
    memcpy(p + 1, str, n);
    return n + 1;
}

static size_t mp_int(uint8_t *p, int v)
{
    if (v >= 0 && v < 128) {
        p[0] = (uint8_t)v;
        return 1;
    }
    p[0] = 0xD1;                  /* int16 */
    p[1] = (uint8_t)((uint16_t)v >> 8);
    p[2] = (uint8_t)((uint16_t)v & 0xFF);
    return 3;
}

static void send_kernel(shell_t *s, const uint8_t *data, size_t len)
{
    fmrb_msg_t msg = {
        .type = FMRB_MSG_TYPE_APP_CONTROL,
        .src_pid = s->ctx->app_id,
        .size = (uint32_t)len,
    };
    memcpy(msg.data, data, len);
    fmrb_err_t ret = fmrb_msg_send(PROC_ID_KERNEL, &msg, 100);
    if (ret != FMRB_OK) {
        FMRB_LOGW(TAG, "[%s] message to the kernel not sent: %d", s->ctx->app_name, ret);
    }
}

/* request_window_menu: {"cmd" => "window_menu", "x", "y", "movable"}. An app
   on the shell is never written to an image, so movable is false. */
static void request_window_menu(shell_t *s, int x, int y)
{
    uint8_t b[48];
    size_t p = 0;
    b[p++] = 0x84;
    p += mp_str(b + p, "cmd");
    p += mp_str(b + p, "window_menu");
    p += mp_str(b + p, "x");
    p += mp_int(b + p, x);
    p += mp_str(b + p, "y");
    p += mp_int(b + p, y);
    p += mp_str(b + p, "movable");
    b[p++] = 0xC2;
    send_kernel(s, b, p);
}

static void send_exit(shell_t *s)
{
    uint8_t b[24];
    size_t p = 0;
    b[p++] = 0x82;
    p += mp_str(b + p, "cmd");
    p += mp_str(b + p, "exit");
    p += mp_str(b + p, "expected");
    b[p++] = 0xC3;
    send_kernel(s, b, p);
}

/* ---- reading a control message ------------------------------------------ */

/* The few things the shell reads from an APP_CONTROL map: "cmd" and some
   integer or boolean values at the top level. A small reader, so a message
   costs no allocation; anything it cannot read leaves the field unset. */
typedef struct {
    char cmd[24];
    int  width, height, x, y;
    int  fullscreen;              /* -1 absent or nil, else 0 / 1 */
} control_t;

/* Skip one msgpack value; NULL when it runs past the end. */
static const uint8_t *mp_skip(const uint8_t *p, const uint8_t *e, int depth)
{
    if (p >= e || depth > 8) return NULL;
    uint8_t t = *p++;
    uint32_t n = 0;
    if (t <= 0x7F || t >= 0xE0 || t == 0xC0 || t == 0xC2 || t == 0xC3) return p;
    if ((t & 0xE0) == 0xA0) { n = t & 0x1F; return (p + n <= e) ? p + n : NULL; }
    if ((t & 0xF0) == 0x90 || (t & 0xF0) == 0x80) {
        n = (uint32_t)(t & 0x0F) * (((t & 0xF0) == 0x80) ? 2u : 1u);
        while (n-- > 0 && p) p = mp_skip(p, e, depth + 1);
        return p;
    }
    switch (t) {
    case 0xCC: case 0xD0: return p + 1 <= e ? p + 1 : NULL;
    case 0xCD: case 0xD1: return p + 2 <= e ? p + 2 : NULL;
    case 0xCE: case 0xD2: case 0xCA: return p + 4 <= e ? p + 4 : NULL;
    case 0xCF: case 0xD3: case 0xCB: return p + 8 <= e ? p + 8 : NULL;
    case 0xD9: case 0xC4:
        if (p + 1 > e) return NULL;
        n = p[0]; p += 1; return (p + n <= e) ? p + n : NULL;
    case 0xDA: case 0xC5:
        if (p + 2 > e) return NULL;
        n = ((uint32_t)p[0] << 8) | p[1]; p += 2; return (p + n <= e) ? p + n : NULL;
    case 0xDC: case 0xDE:
        if (p + 2 > e) return NULL;
        n = ((uint32_t)p[0] << 8) | p[1]; p += 2;
        if (t == 0xDE) n *= 2;
        while (n-- > 0 && p) p = mp_skip(p, e, depth + 1);
        return p;
    default:
        return NULL;   /* larger containers and ext types: not in a control */
    }
}

/* An integer (or boolean) value at p; false when it is something else. */
static bool mp_read_int(const uint8_t *p, const uint8_t *e, int *out)
{
    if (p >= e) return false;
    uint8_t t = p[0];
    if (t <= 0x7F) { *out = t; return true; }
    if (t >= 0xE0) { *out = (int8_t)t; return true; }
    if (t == 0xC2 || t == 0xC3) { *out = (t == 0xC3); return true; }
    if (t == 0xCC && p + 2 <= e) { *out = p[1]; return true; }
    if (t == 0xD0 && p + 2 <= e) { *out = (int8_t)p[1]; return true; }
    if ((t == 0xCD || t == 0xD1) && p + 3 <= e) {
        uint16_t v = (uint16_t)((p[1] << 8) | p[2]);
        *out = (t == 0xD1) ? (int16_t)v : v;
        return true;
    }
    if ((t == 0xCE || t == 0xD2) && p + 5 <= e) {
        uint32_t v = ((uint32_t)p[1] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 8) | p[4];
        *out = (int)v;
        return true;
    }
    return false;
}

static bool read_control(const uint8_t *d, uint32_t size, control_t *c)
{
    memset(c, 0, sizeof(*c));
    c->fullscreen = -1;
    c->width = c->height = -1;
    const uint8_t *p = d, *e = d + size;
    if (p >= e) return false;
    uint32_t pairs;
    if ((p[0] & 0xF0) == 0x80) {
        pairs = p[0] & 0x0F;
        p += 1;
    } else if (p[0] == 0xDE && p + 3 <= e) {
        pairs = ((uint32_t)p[1] << 8) | p[2];
        p += 3;
    } else {
        return false;
    }
    while (pairs-- > 0 && p && p < e) {
        if ((p[0] & 0xE0) != 0xA0) {        /* only fixstr keys are read */
            p = mp_skip(p, e, 0);
            p = p ? mp_skip(p, e, 0) : NULL;
            continue;
        }
        size_t kl = p[0] & 0x1F;
        const char *k = (const char *)(p + 1);
        const uint8_t *v = p + 1 + kl;
        if (v > e) return false;
        if (kl == 3 && memcmp(k, "cmd", 3) == 0 && v < e && (v[0] & 0xE0) == 0xA0) {
            size_t n = v[0] & 0x1F;
            if (n < sizeof(c->cmd) && v + 1 + n <= e) {
                memcpy(c->cmd, v + 1, n);
                c->cmd[n] = '\0';
            }
        } else if (kl == 5 && memcmp(k, "width", 5) == 0) {
            mp_read_int(v, e, &c->width);
        } else if (kl == 6 && memcmp(k, "height", 6) == 0) {
            mp_read_int(v, e, &c->height);
        } else if (kl == 1 && k[0] == 'x') {
            mp_read_int(v, e, &c->x);
        } else if (kl == 1 && k[0] == 'y') {
            mp_read_int(v, e, &c->y);
        } else if (kl == 10 && memcmp(k, "fullscreen", 10) == 0) {
            int f;
            if (mp_read_int(v, e, &f)) c->fullscreen = f ? 1 : 0;
        }
        p = mp_skip(v, e, 0);
    }
    return c->cmd[0] != '\0';
}

/* ---- the frame's events and the system's controls ----------------------- */

static void stop(shell_t *s)
{
    s->running = false;
    s->ctx->expected_stop = true;
}

/* _frame_event: the close button and the title bar's right click; for a
   frameless window, a right click anywhere. */
static void frame_event(shell_t *s, const fmrb_shell_event_t *ev)
{
    if (s->canvas < 0) return;
    bool down = ev->kind == FMRB_HID_EVENT_MOUSE_DOWN;
    bool up = ev->kind == FMRB_HID_EVENT_MOUSE_UP;
    if (s->frameless) {
        if (up && ev->button == 3 && !s->fullscreen) {
            request_window_menu(s, ev->wx, ev->wy);
        }
        return;
    }
    if (ev->button == 1 && (down || up)) {
        int cx = s->ww - CLOSE_BTN_CX_OFFSET;
        int cy = CLOSE_BTN_CY;
        int dx = ev->wx - cx, dy = ev->wy - cy;
        bool hit = (dx < 0 ? -dx : dx) <= CLOSE_BTN_HIT_R &&
                   (dy < 0 ? -dy : dy) <= CLOSE_BTN_HIT_R;
        if (down) {
            if (hit && !s->fullscreen) {
                s->close_pressed = true;
                circle(s, cx, cy, CLOSE_BTN_R, CLOSE_BTN_PRESSED_COLOR);
                present(s);
            }
        } else if (s->close_pressed) {
            s->close_pressed = false;
            if (hit) {
                stop(s);
            } else {
                circle(s, cx, cy, CLOSE_BTN_R, fmrb_theme_get()->text_light);
                present(s);
            }
        } else if (hit && !s->fullscreen) {
            stop(s);   /* the press was missed, the release is on the button */
        }
    }
    if (up && ev->button == 3 && ev->wy < TITLE_BAR_H && !s->fullscreen) {
        request_window_menu(s, ev->wx, ev->wy);
    }
}

static void dispatch_hid(shell_t *s, const fmrb_msg_t *msg)
{
    fmrb_hid_event_t h;
    if (!fmrb_hid_event_decode(msg->data, msg->size, &h)) {
        return;
    }
    fmrb_shell_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = (int)h.type;
    switch (h.type) {
    case FMRB_HID_EVENT_KEY_DOWN:
    case FMRB_HID_EVENT_KEY_UP:
        ev.keycode = h.keycode;
        ev.scancode = h.scancode;
        ev.modifier = h.modifier;
        ev.character = (uint8_t)h.character;
        break;
    case FMRB_HID_EVENT_MOUSE_DOWN:
    case FMRB_HID_EVENT_MOUSE_UP:
    case FMRB_HID_EVENT_MOUSE_MOVE:
    case FMRB_HID_EVENT_MOUSE_WHEEL:
        ev.button = h.button;
        ev.delta = h.wheel;
        /* The kernel sends the window's coordinates; the app's are the area's. */
        ev.wx = h.x;
        ev.wy = h.y;
        ev.x = (int)h.x - s->ax;
        ev.y = (int)h.y - s->ay;
        break;
    case FMRB_HID_EVENT_GAMEPAD_DOWN:
    case FMRB_HID_EVENT_GAMEPAD_UP:
        ev.gamepad_id = h.gamepad_id;
        ev.button = h.button;
        break;
    case FMRB_HID_EVENT_GAMEPAD_AXIS:
        ev.gamepad_id = h.gamepad_id;
        ev.axis = h.axis;
        ev.value = h.value;
        break;
    case FMRB_HID_EVENT_KANA_MODE:
        ev.mode = h.kana_mode;
        break;
    default:
        return;
    }
    frame_event(s, &ev);
    if (s->running && s->eng->has_hook(s->ud, FMRB_SHELL_HOOK_EVENT)) {
        call_begin(s);
        call_end(s, s->eng->call_event(s->ud, &ev));
    }
}

static void on_resize_message(shell_t *s, const control_t *c)
{
    if (c->width <= 0 || c->height <= 0) return;
    s->ww = (uint16_t)c->width;
    s->wh = (uint16_t)c->height;
    if (c->fullscreen >= 0) {
        s->fullscreen = c->fullscreen != 0;
    }
    layout(s);
    clear_area(s, -1);
    if (s->canvas >= 0) present(s);
    if (s->eng->has_hook(s->ud, FMRB_SHELL_HOOK_RESIZE)) {
        call_begin(s);
        call_end(s, s->eng->call_resize(s->ud, s->ww, s->wh));
    }
}

static void dispatch_control(shell_t *s, const fmrb_msg_t *msg)
{
    control_t c;
    if (!read_control(msg->data, msg->size, &c)) return;
    const char *cmd = c.cmd;
    if (strcmp(cmd, "resize") == 0) {
        on_resize_message(s, &c);
    } else if (strcmp(cmd, "suspend") == 0) {
        s->suspended = true;
        call_hook(s, FMRB_SHELL_HOOK_SUSPEND);
        FMRB_LOGI(TAG, "App %s suspended", s->ctx->app_name);
    } else if (strcmp(cmd, "resume") == 0) {
        s->suspended = false;
        call_hook(s, FMRB_SHELL_HOOK_RESUME);
        FMRB_LOGI(TAG, "App %s resumed", s->ctx->app_name);
    } else if (strcmp(cmd, "stop") == 0) {
        FMRB_LOGI(TAG, "App %s received stop command", s->ctx->app_name);
        stop(s);
    } else if (strcmp(cmd, "clear_and_stop") == 0) {
        if (s->canvas >= 0) {
            gfx_cmd_t cmdc;
            fmrb_gfx_cmd_clear(&cmdc, (fmrb_canvas_handle_t)s->canvas, 0x00);
            submit(&cmdc);
            present(s);
        }
        stop(s);
    } else if (strcmp(cmd, "quit_request") == 0) {
        /* Ctrl+Q: the app may ask first; without on_quit_request it closes. */
        if (s->eng->has_hook(s->ud, FMRB_SHELL_HOOK_QUIT_REQUEST)) {
            call_hook(s, FMRB_SHELL_HOOK_QUIT_REQUEST);
        } else {
            stop(s);
        }
    } else if (strcmp(cmd, "long_press") == 0) {
        if (s->frameless && !s->fullscreen) {
            request_window_menu(s, c.x, c.y);
        }
    }
    /* "moved" needs nothing: present reads the place from the context.
       Other controls (topics, run results) are not offered to these apps. */
}

/* _spin: deliver messages until the wait is over or the app is ending. */
static void wait_and_dispatch(shell_t *s, int timeout_ms)
{
    fmrb_tick_t target = fmrb_task_get_tick_count() + FMRB_MS_TO_TICKS(timeout_ms);
    while (s->running) {
        if (s->ctx->should_exit) {
            stop(s);
            break;
        }
        fmrb_tick_t now = fmrb_task_get_tick_count();
        if (now >= target) break;
        fmrb_msg_t msg;
        fmrb_err_t ret = fmrb_msg_receive(s->ctx->app_id, &msg,
                                          FMRB_TICKS_TO_MS(target - now));
        if (ret == FMRB_ERR_TIMEOUT) break;
        if (ret != FMRB_OK) {
            FMRB_LOGW(TAG, "App %s message receive error: %d", s->ctx->app_name, ret);
            break;
        }
        if (msg.type == FMRB_MSG_TYPE_HID_EVENT) {
            dispatch_hid(s, &msg);
        } else if (msg.type == FMRB_MSG_TYPE_APP_CONTROL) {
            dispatch_control(s, &msg);
        }
    }
}

/* ---- the life ----------------------------------------------------------- */

static bool exports_any(shell_t *s)
{
    return s->eng->has_hook(s->ud, FMRB_SHELL_HOOK_CREATE) ||
           s->eng->has_hook(s->ud, FMRB_SHELL_HOOK_UPDATE) ||
           s->eng->has_hook(s->ud, FMRB_SHELL_HOOK_EVENT);
}

fmrb_err_t fmrb_app_shell_run(fmrb_app_task_context_t *ctx,
                              const fmrb_shell_engine_t *engine, void *ud,
                              char *why, size_t why_cap)
{
    if (why && why_cap) why[0] = '\0';
    if (!ctx || !engine) return FMRB_ERR_INVALID_PARAM;

    shell_t s;
    memset(&s, 0, sizeof(s));
    s.ctx = ctx;
    s.eng = engine;
    s.ud = ud;
    s.canvas = -1;
    s.running = true;
    ctx->shell = &s;

    fmrb_err_t result = FMRB_OK;

    /* The program's top level: it defines the functions. */
    int rc = call_hook(&s, FMRB_SHELL_HOOK_LOAD);
    if (rc == FMRB_SHELL_RAISED) {
        result = FMRB_ERR_FAILED;
        goto out;
    }
    if (rc == FMRB_SHELL_CLOSED) goto out;
    s.running = true;

    if (!exports_any(&s)) {
        const char *path = (ctx->load_mode == FMRB_LOAD_MODE_FILE && ctx->load_data)
                               ? (const char *)ctx->load_data : "this program";
        if (why && why_cap) {
            snprintf(why, why_cap,
                     "No on_create, on_update or on_event in %s.\n"
                     "Define one: function on_update() ... end", path);
        }
        result = FMRB_ERR_FAILED;
        goto out;
    }

    /* The window (FmrbApp#_init). */
    fmrb_canvas_handle_t canvas = FMRB_CANVAS_SCREEN, bg = FMRB_CANVAS_SCREEN;
    if (fmrb_app_canvas_init(ctx, &canvas, &bg) != FMRB_OK) {
        if (why && why_cap) snprintf(why, why_cap, "Failed to create the window.");
        result = FMRB_ERR_FAILED;
        goto out;
    }
    if (canvas != FMRB_CANVAS_SCREEN) s.canvas = (int)canvas;
    s.bg = (bg != FMRB_CANVAS_SCREEN);
    s.fullscreen = ctx->fullscreen;
    s.frameless = ctx->frameless;
    s.rounded = !ctx->fullscreen && ctx->rounded_corners;
    s.resizable = ctx->resizable;
    s.ww = ctx->window_width;
    s.wh = ctx->window_height;
    layout(&s);
    if (s.canvas >= 0) {
        clear_area(&s, -1);
        present(&s);
    }

    call_hook(&s, FMRB_SHELL_HOOK_CREATE);

    /* main_loop */
    while (s.running) {
        if (ctx->should_exit) {
            stop(&s);
            break;
        }
        if (s.suspended) {
            wait_and_dispatch(&s, SUSPENDED_WAIT_MS);
            continue;
        }
        int next_ms = FMRB_SHELL_DEFAULT_UPDATE_MS;
        if (s.eng->has_hook(s.ud, FMRB_SHELL_HOOK_UPDATE)) {
            int ms = -1;
            call_begin(&s);
            call_end(&s, s.eng->call_update(s.ud, &ms));
            if (ms >= 0) next_ms = ms;
        }
        if (!s.running) break;
        if (next_ms == 0) {
            fmrb_task_delay(0);   /* let the others run, as Task.pass does */
        }
        wait_and_dispatch(&s, next_ms);
    }
    if (s.raised) {
        result = FMRB_ERR_FAILED;
        goto out;
    }

    /* destroy: tell the kernel first, so the window leaves its list while
       the app can still be talked to; then on_destroy. The task wrapper
       releases the canvases and the queue. */
    send_exit(&s);
    s.running = true;   /* let on_destroy run */
    call_hook(&s, FMRB_SHELL_HOOK_DESTROY);
    if (s.raised) result = FMRB_ERR_FAILED;

out:
    ctx->shell = NULL;
    return result;
}

/* ---- for the binding ---------------------------------------------------- */

bool fmrb_app_shell_layout(fmrb_app_task_context_t *ctx, fmrb_shell_layout_t *out)
{
    shell_t *s = shell_of(ctx);
    if (!s) {
        return false;
    }
    if (out) {
        *out = (fmrb_shell_layout_t){
            .canvas = s->canvas,
            .window_w = s->ww, .window_h = s->wh,
            .area_x = s->ax, .area_y = s->ay,
            .area_w = s->aw, .area_h = s->ah,
            .fullscreen = s->fullscreen,
        };
    }
    return true;
}

void fmrb_app_shell_stop(fmrb_app_task_context_t *ctx)
{
    shell_t *s = shell_of(ctx);
    if (s) stop(s);
}

void fmrb_app_shell_draw_frame(fmrb_app_task_context_t *ctx)
{
    shell_t *s = shell_of(ctx);
    if (s) draw_frame(s);
}

void fmrb_app_shell_clear_user_area(fmrb_app_task_context_t *ctx, int color)
{
    shell_t *s = shell_of(ctx);
    if (s) clear_area(s, color);
}

bool fmrb_app_shell_unwind_now(fmrb_app_task_context_t *ctx)
{
    shell_t *s = shell_of(ctx);
    if (!s) {
        return fmrb_app_poll_exit_signal(ctx);
    }
    if (!s->in_call) {
        return false;
    }
    fmrb_time_t ran = fmrb_hal_time_get_us() - s->call_started_us;
    if (ran < (fmrb_time_t)FMRB_SHELL_LONG_CALL_MS * 1000) {
        return false;
    }
    return fmrb_app_poll_exit_signal(ctx);
}
