/*
 * The app boundary's FmrbApp flat functions (doc/app_boundary): the C bodies
 * behind fmrb_gen/fmrb_boundary.h, generated from sig/fmrb_app.rbs. Every
 * engine's glue (mruby, Lua, MicroPython, Spinel, WAMR) is generated and
 * calls these; nothing here knows an engine. FmrbApp's functions take no
 * receiver number (the receiver is the calling app).
 *
 * The window's state and the requests are for the engines on the C app
 * shell (Lua, wasm): the Ruby engines answer the readers (window_width ...),
 * stop, draw_window_frame and clear_user_area_raw from their own base class,
 * which keeps that state itself (mrblib defines them again after the gem's
 * C; MicroPython's prelude does the same), and call the requests here. Off
 * the shell the readers answer from the app's context.
 *
 * Results follow fmrb_gen/fmrb_boundary.h: 0 or more on success,
 * FMRB_BOUNDARY_ERR_FAILED, or FMRB_BOUNDARY_ERR_ARG for an argument out of
 * range.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fmrb_gen/fmrb_boundary.h"
#include "fmrb_boundary_msg.h"
#include "fmrb_app.h"
#include "fmrb_app_shell.h"
#include "fmrb_clipboard.h"
#include "fmrb_err.h"
#include "fmrb_hal_time.h"
#include "fmrb_mem.h"
#include "fmrb_theme.h"
#include "fmrb_platform_sys.h"
#include "host_task.h"
#ifdef FMRB_HAVE_WASM
#include "fmrb_wasm.h"
#endif

static const char *TAG = "app";

#define ERR_FAILED FMRB_BOUNDARY_ERR_FAILED
#define ERR_ARG    FMRB_BOUNDARY_ERR_ARG

/* ---- the clipboard (doc/linux_native L12a) -------------------------------- */

/* The first n bytes of b without a last character that is not all there. */
static size_t utf8_whole(const unsigned char *b, size_t n)
{
    size_t i = n;
    for (int k = 0; i > 0 && k < 4; k++) {
        i--;
        if ((b[i] & 0xC0) != 0x80) {
            break;
        }
    }
    if (i >= n) {
        return n;
    }
    unsigned char c = b[i];
    size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    return (n - i >= need) ? n : i;
}

// The text every app shares, at most limit bytes when limit > 0 (cut before
// a character that would not fit whole). Read under the clipboard's lock, so
// one call sees one version. Returns the length of the whole answer; a
// buffer too small for it gets as many whole characters as fit (the glue
// asks again with the length, and a clipboard that changed in between still
// gives text that is not cut inside a character).
int32_t fmrb_bnd_app_clipboard_raw(int32_t limit, char *out, int32_t out_size)
{
    if (out_size < 0 || (out_size > 0 && !out)) {
        return ERR_ARG;
    }
    size_t len = 0;
    const char *text = fmrb_clipboard_lock(&len);
    if (!text) {
        return 0;
    }
    size_t want = (limit > 0 && (size_t)limit < len) ? (size_t)limit : len;
    if (want < len) {
        want = utf8_whole((const unsigned char *)text, want);
    }
    size_t n = want <= (size_t)out_size ? want : utf8_whole((const unsigned char *)text, (size_t)out_size);
    if (n > 0) {
        memcpy(out, text, n);
    }
    fmrb_clipboard_unlock();
    return want > INT32_MAX ? INT32_MAX : (int32_t)want;
}

// false (0) when the text is over the clipboard's maximum (64 KB).
int32_t fmrb_bnd_app_set_clipboard(const char *text, int32_t text_len)
{
    if (!text || text_len < 0) {
        return ERR_ARG;
    }
    return fmrb_clipboard_set(text, (size_t)text_len) == FMRB_OK ? 1 : 0;
}

int32_t fmrb_bnd_app_clipboard_gen(void)
{
    return (int32_t)(fmrb_clipboard_generation() & 0x7fffffffu);
}

/* ---- the clocks ------------------------------------------------------------ */

// Microseconds since boot, for timing short sections (64 bits: it passes
// int32 after 35 minutes).
int64_t fmrb_bnd_app_uptime_us(void)
{
    return (int64_t)fmrb_hal_time_get_us();
}

// The date and time now, local; 0 (nil) when the clock has never been set.
int32_t fmrb_bnd_app_wallclock(fmrb_bnd_app_wallclock_t *out)
{
    fmrb_wallclock_t wc;
    if (fmrb_hal_time_get_wallclock(&wc) != FMRB_OK) {
        return 0;
    }
    out->year = wc.year;
    out->month = wc.month;
    out->day = wc.day;
    out->hour = wc.hour;
    out->minute = wc.minute;
    out->second = wc.second;
    return 1;
}

// Milliseconds since boot (64 bits, as uptime_us).
int64_t fmrb_bnd_app_uptime_ms(void)
{
    return (int64_t)(fmrb_hal_time_get_us() / 1000);
}

/* ---- the window ------------------------------------------------------------ */

// The layout the shell gave, or the same worked out from the context off the
// shell (FmrbApp#_layout_user_area): a fullscreen or frameless window owns all
// of it, a framed one loses the border and the 11-dot title bar.
static fmrb_app_task_context_t *app_layout(fmrb_shell_layout_t *l)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return NULL;
    }
    if (fmrb_app_shell_layout(ctx, l)) {
        return ctx;
    }
    l->canvas = ctx->headless ? -1 : (int)ctx->canvas_id;
    l->window_w = ctx->window_width;
    l->window_h = ctx->window_height;
    l->fullscreen = ctx->fullscreen;
    if (ctx->fullscreen || ctx->frameless || ctx->has_background_canvas) {
        l->area_x = 0;
        l->area_y = 0;
        l->area_w = ctx->window_width;
        l->area_h = ctx->window_height;
    } else {
        l->area_x = 1;
        l->area_y = 11;
        l->area_w = (uint16_t)(ctx->window_width > 2 ? ctx->window_width - 2 : 0);
        l->area_h = (uint16_t)(ctx->window_height > 12 ? ctx->window_height - 12 : 0);
    }
    return ctx;
}

int32_t fmrb_bnd_app_name(char *out, int32_t out_size)
{
    if (out_size < 0 || (out_size > 0 && !out)) {
        return ERR_ARG;
    }
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return ERR_FAILED;
    }
    size_t n = strnlen(ctx->app_name, sizeof(ctx->app_name));
    memcpy(out, ctx->app_name, n < (size_t)out_size ? n : (size_t)out_size);
    return (int32_t)n;
}

int32_t fmrb_bnd_app_window_width(void)
{
    fmrb_shell_layout_t l;
    return app_layout(&l) ? (int32_t)l.window_w : ERR_FAILED;
}

int32_t fmrb_bnd_app_window_height(void)
{
    fmrb_shell_layout_t l;
    return app_layout(&l) ? (int32_t)l.window_h : ERR_FAILED;
}

int32_t fmrb_bnd_app_user_area_width(void)
{
    fmrb_shell_layout_t l;
    return app_layout(&l) ? (int32_t)l.area_w : ERR_FAILED;
}

int32_t fmrb_bnd_app_user_area_height(void)
{
    fmrb_shell_layout_t l;
    return app_layout(&l) ? (int32_t)l.area_h : ERR_FAILED;
}

int32_t fmrb_bnd_app_pos_x(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    return ctx ? (int32_t)ctx->window_pos_x : ERR_FAILED;
}

int32_t fmrb_bnd_app_pos_y(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    return ctx ? (int32_t)ctx->window_pos_y : ERR_FAILED;
}

// :esp32 (0) or :linux (1): the machine the app runs on now, as
// FmrbConst::PLATFORM says it (the browser build says linux too).
int32_t fmrb_bnd_app_platform(void)
{
#ifdef CONFIG_IDF_TARGET_LINUX
    return FMRB_BND_APP_PLATFORM_LINUX;
#else
    return FMRB_BND_APP_PLATFORM_ESP32;
#endif
}

int32_t fmrb_bnd_app_fullscreen(void)
{
    fmrb_shell_layout_t l;
    return app_layout(&l) ? (l.fullscreen ? 1 : 0) : ERR_FAILED;
}

int32_t fmrb_bnd_app_rounded_corners(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    return ctx ? (ctx->rounded_corners ? 1 : 0) : ERR_FAILED;
}

int32_t fmrb_bnd_app_frameless(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    return ctx ? (ctx->frameless ? 1 : 0) : ERR_FAILED;
}

int32_t fmrb_bnd_app_resizable(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    return ctx ? (ctx->resizable ? 1 : 0) : ERR_FAILED;
}

// The shell's: an app that is not on it (a Ruby or Python app) has its own.
int32_t fmrb_bnd_app_stop(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx || !fmrb_app_shell_layout(ctx, NULL)) {
        return ERR_FAILED;
    }
    fmrb_app_shell_stop(ctx);
    return 0;
}

int32_t fmrb_bnd_app_draw_window_frame(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx || !fmrb_app_shell_layout(ctx, NULL)) {
        return ERR_FAILED;
    }
    fmrb_app_shell_draw_frame(ctx);
    return 0;
}

int32_t fmrb_bnd_app_clear_user_area_raw(int32_t color)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx || !fmrb_app_shell_layout(ctx, NULL)) {
        return ERR_FAILED;
    }
    if (color > 0xFF) {
        return ERR_ARG;
    }
    fmrb_app_shell_clear_user_area(ctx, color);
    return 0;
}

int32_t fmrb_bnd_app_theme_bg(void)
{
    return fmrb_theme_get()->window_bg;
}

int32_t fmrb_bnd_app_theme_fg(void)
{
    return fmrb_theme_get()->text;
}

int32_t fmrb_bnd_app_theme_accent(void)
{
    return fmrb_theme_get()->highlight;
}

int32_t fmrb_bnd_app_theme_border(void)
{
    return fmrb_theme_get()->border;
}

int32_t fmrb_bnd_app_theme_fg_light(void)
{
    return fmrb_theme_get()->text_light;
}

// Where the window is: the context's place, and the kernel's copy of the
// window list no longer says where it is. The next present puts it there
// (the Ruby bases present at once; on the shell the app's next present does).
int32_t fmrb_bnd_app_set_window_position(int32_t x, int32_t y)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return ERR_FAILED;
    }
    if (x < 0 || x > 0xFFFF || y < 0 || y > 0xFFFF) {
        return ERR_ARG;
    }
    ctx->window_pos_x = (uint16_t)x;
    ctx->window_pos_y = (uint16_t)y;
    fmrb_app_window_moved();
    return 0;
}

/* ---- requests to the kernel -------------------------------------------------- */

static int32_t send_cmd(const char *cmd)
{
    fmrb_bnd_cmd_t c;
    if (!fmrb_bnd_cmd_begin(&c, FMRB_MSG_TYPE_APP_CONTROL, cmd, 1)) {
        return ERR_FAILED;
    }
    return fmrb_bnd_cmd_send(&c, TAG, cmd);
}

// {"cmd" => "reload_confirm"}: the desktop asks whether to start the app
// again from its file.
int32_t fmrb_bnd_app_request_reload(void)
{
    return send_cmd("reload_confirm");
}

// {"cmd" => "close_confirm"}: "Close <app>?" on the desktop.
int32_t fmrb_bnd_app_request_close_confirm(void)
{
    return send_cmd("close_confirm");
}

// {"cmd" => "window_menu", "x", "y", "movable"} at (x, y) of the window.
// movable: the app can be written to an image (an app on mruby that is
// movable, or was started from one) -- what Send to needs.
int32_t fmrb_bnd_app_request_window_menu(int32_t x, int32_t y)
{
    fmrb_bnd_cmd_t c;
    fmrb_app_task_context_t *ctx = fmrb_bnd_cmd_begin(&c, FMRB_MSG_TYPE_APP_CONTROL, "window_menu", 4);
    if (!ctx) {
        return ERR_FAILED;
    }
    bool movable = false;
#ifdef FMRB_HAVE_CHECKPOINT
    movable = ctx->vm_type == FMRB_VM_TYPE_MRUBY && ctx->spinel_program == NULL &&
              (ctx->movable || ctx->load_mode == FMRB_LOAD_MODE_CHECKPOINT);
#endif
    put_key(&c.out, "x");
    put_int(&c.out, x);
    put_key(&c.out, "y");
    put_int(&c.out, y);
    put_key(&c.out, "movable");
    put_bool(&c.out, movable);
    return fmrb_bnd_cmd_send(&c, TAG, "window_menu");
}

// {"cmd" => "enter_fullscreen"} or "exit_fullscreen"; the answer comes as a
// resize.
int32_t fmrb_bnd_app_request_fullscreen(int32_t on)
{
    return send_cmd(on ? "enter_fullscreen" : "exit_fullscreen");
}

int32_t fmrb_bnd_app_toggle_fullscreen(void)
{
    fmrb_shell_layout_t l;
    if (!app_layout(&l)) {
        return ERR_FAILED;
    }
    return send_cmd(l.fullscreen ? "exit_fullscreen" : "enter_fullscreen");
}

// {"cmd" => "file_select", "mode" => mode}; the chosen path comes to
// on_control.
int32_t fmrb_bnd_app_request_file_select_raw(const char *mode, int32_t mode_len)
{
    if (!mode || mode_len < 0) {
        return ERR_ARG;
    }
    fmrb_bnd_cmd_t c;
    if (!fmrb_bnd_cmd_begin(&c, FMRB_MSG_TYPE_APP_CONTROL, "file_select", 2)) {
        return ERR_FAILED;
    }
    put_key(&c.out, "mode");
    put_str(&c.out, mode, (size_t)mode_len);
    return fmrb_bnd_cmd_send(&c, TAG, "file_select");
}

// {"cmd" => "run", "path", "prev_pid" (nil when negative)}; the new pid comes
// to on_control as run_result.
int32_t fmrb_bnd_app_request_run_raw(const char *path, int32_t path_len, int32_t prev_pid)
{
    if (!path || path_len < 0) {
        return ERR_ARG;
    }
    fmrb_bnd_cmd_t c;
    if (!fmrb_bnd_cmd_begin(&c, FMRB_MSG_TYPE_APP_CONTROL, "run", 3)) {
        return ERR_FAILED;
    }
    put_key(&c.out, "path");
    put_str(&c.out, path, (size_t)path_len);
    put_key(&c.out, "prev_pid");
    if (prev_pid < 0) {
        put_nil(&c.out);
    } else {
        put_int(&c.out, prev_pid);
    }
    return fmrb_bnd_cmd_send(&c, TAG, "run");
}

/* ---- the machine, as an app reads it -------------------------------------- */

// "en" or "ja", the setting the user chose (read each time: the Config app
// changes it).
int32_t fmrb_bnd_app_language(char *out, int32_t out_size)
{
    if (out_size < 0 || (out_size > 0 && !out)) {
        return ERR_ARG;
    }
    const fmrb_system_config_t *cfg = fmrb_kernel_get_config();
    const char *lang = (cfg && cfg->language[0]) ? cfg->language : "en";
    size_t n = strlen(lang);
    memcpy(out, lang, n < (size_t)out_size ? n : (size_t)out_size);
    return (int32_t)n;
}

// A notice at the top right of the PC's screen; false elsewhere. The texts
// are cut to fit (a notice shows two lines).
int32_t fmrb_bnd_app_notify_raw(const char *summary, int32_t summary_len, const char *body, int32_t body_len)
{
    if (!summary || summary_len < 0 || !body || body_len < 0) {
        return ERR_ARG;
    }
    fmrb_app_task_context_t *ctx = fmrb_current();
    char s[128];
    char b[256];
    size_t sn = (size_t)summary_len < sizeof(s) ? (size_t)summary_len : sizeof(s) - 1;
    size_t bn = (size_t)body_len < sizeof(b) ? (size_t)body_len : sizeof(b) - 1;
    memcpy(s, summary, sn);
    s[sn] = '\0';
    memcpy(b, body, bn);
    b[bn] = '\0';
    return fmrb_platform_sys_notice_post(ctx ? ctx->app_name : "", s, b, -1) == FMRB_OK ? 1 : 0;
}

int32_t fmrb_bnd_app_ps_gen(void)
{
    return (int32_t)(fmrb_app_proc_generation() & 0x7fffffffu);
}

// The system pool (fmrb_sys_malloc's TLSF); zeros when it cannot be read.
int32_t fmrb_bnd_app_sys_pool_info(fmrb_bnd_app_sys_pool_info_t *out)
{
    fmrb_pool_stats_t st;
    memset(out, 0, sizeof(*out));
    if (fmrb_sys_mem_get_stats(&st) == 0) {
        out->total = (uint32_t)st.total_size;
        out->used = (uint32_t)st.used_size;
        out->free = (uint32_t)st.free_size;
        out->used_blocks = (uint32_t)st.used_blocks;
        out->free_blocks = (uint32_t)st.free_blocks;
    }
    return 1;
}

// The host task's counters (they wrap at 2^32; take rates from deltas).
int32_t fmrb_bnd_app_gfx_stats(fmrb_bnd_app_gfx_stats_t *out)
{
    uint32_t cmds = 0, presents = 0;
    fmrb_host_get_gfx_counters(&cmds, &presents);
    out->cmds = cmds;
    out->presents = presents;
    return 1;
}

// The wasm runtime (components/fmrb_wamr); zeros on a machine without it.
int32_t fmrb_bnd_app_wasm_stats(fmrb_bnd_app_wasm_stats_t *out)
{
    memset(out, 0, sizeof(*out));
#ifdef FMRB_HAVE_WASM
    fmrb_wasm_stats_t st;
    fmrb_wasm_stats(&st);
    out->running = st.running ? 1 : 0;
    out->apps = (uint8_t)st.apps;
    out->modules = (uint8_t)st.modules;
    out->natives = (uint16_t)st.natives;
    out->pool_total = st.pool_total;
    out->pool_used = st.pool_used;
    out->pool_high = st.pool_high;
    out->traps = st.traps;
#endif
    return 1;
}

// One wasm app; 0 (nil) when pid is not a wasm app with its module loaded.
int32_t fmrb_bnd_app_wasm_app_stats(int32_t pid, fmrb_bnd_app_wasm_app_stats_t *out)
{
    memset(out, 0, sizeof(*out));
#ifdef FMRB_HAVE_WASM
    fmrb_wasm_app_stats_t st;
    if (!fmrb_wasm_app_stats((int)pid, &st)) {
        return 0;
    }
    out->pages = st.pages;
    out->max_pages = st.max_pages;
    out->binary_size = st.binary_size;
    out->hook_us = st.hook_us;
    out->hook_max_us = st.hook_max_us;
    out->calls = st.calls;
    return 1;
#else
    (void)pid;
    return 0;
#endif
}

// Where this app's text cursor is, for a PC's IME (h = 0: no text now).
int32_t fmrb_bnd_app_set_text_cursor(int32_t x, int32_t y, int32_t h)
{
    fmrb_host_set_text_cursor((int)x, (int)y, (int)h);
    return 0;
}

int32_t fmrb_bnd_app_audio_muted(void)
{
    return fmrb_host_audio_muted() ? 1 : 0;
}

int32_t fmrb_bnd_app_audio_volume(void)
{
    return (int32_t)fmrb_host_audio_volume();
}

int32_t fmrb_bnd_app_wifi_connected(void)
{
    return fmrb_platform_sys_net_connected() ? 1 : 0;
}

int32_t fmrb_bnd_app_pid(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    return ctx ? (int32_t)ctx->app_id : ERR_FAILED;
}

// The file the app was started from; "" for one built into the firmware
// (its builtin_path for one that goes by a path name, default/transfer).
int32_t fmrb_bnd_app_script_path(char *out, int32_t out_size)
{
    if (out_size < 0 || (out_size > 0 && !out)) {
        return ERR_ARG;
    }
    fmrb_app_task_context_t *ctx = fmrb_current();
    const char *p = ctx ? ctx->filepath : "";
    size_t n = strnlen(p, FMRB_MAX_PATH_LEN);
    memcpy(out, p, n < (size_t)out_size ? n : (size_t)out_size);
    return (int32_t)n;
}

// 0-100, or none where there is no battery value.
int32_t fmrb_bnd_app_battery_level(void)
{
    fmrb_sys_battery_t b;
    if (!fmrb_platform_sys_battery(&b)) {
        return FMRB_BOUNDARY_NONE;
    }
    return (int32_t)b.percent;
}

int32_t fmrb_bnd_app_battery_charging(void)
{
    fmrb_sys_battery_t b;
    return (fmrb_platform_sys_battery(&b) && b.charging) ? 1 : 0;
}

#if defined(FMRB_PLATFORM_NATIVE)
extern void native_crt_state(char *out, size_t cap);
#endif

// The native compositor's CRT look ("" where it cannot be drawn), e.g.
// "crt,55,30,30,15,40,0,0,0" (doc/web_crt).
int32_t fmrb_bnd_app_crt_state(char *out, int32_t out_size)
{
    if (out_size < 0 || (out_size > 0 && !out)) {
        return ERR_ARG;
    }
    char buf[64] = "";
#if defined(FMRB_PLATFORM_NATIVE)
    native_crt_state(buf, sizeof(buf));
#endif
    size_t n = strnlen(buf, sizeof(buf));
    memcpy(out, buf, n < (size_t)out_size ? n : (size_t)out_size);
    return (int32_t)n;
}

int32_t fmrb_bnd_app_linux_available(void)
{
    return fmrb_platform_sys_proc_available() ? 1 : 0;
}
