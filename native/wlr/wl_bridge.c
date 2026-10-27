/*
 * wl_bridge.c - Linux windows as Family mruby windows (doc/linux_native/,
 * L9a): Family mruby's side of the compositor (comp_link.h).
 *
 * Every Linux window gets a stand-in: a small mruby app
 * (main/prebuild_scripts/kernel/linux_window.app.rb) whose window has the
 * Family mruby title bar, frame and close button and whose content is the
 * hole colour (comp_route.h). Being an ordinary app window, the stand-in is
 * moved, resized, raised, focused and stacked by the window manager and the
 * input router exactly as every other window is; this task only watches the
 * result and tells the compositor where each Linux window has to be:
 *
 *   - a window appears (notice): spawn its stand-in, sized to the window
 *     (its size over the picture's scale, rounded up), centred; the kernel
 *     is told as for any spawn outside it ("spawned"), and gives it the
 *     keyboard (and puts it in front).
 *   - a dialog (a window with a parent, L9c): its stand-in comes once the
 *     parent's has (or after PARENT_WAIT_MS), with its content's middle on
 *     the parent's content's middle, kept in the screen. Whenever the
 *     parent's stand-in is above the dialog's (the parent was clicked, or
 *     brought in front any other way), the dialog's is brought in front of
 *     it, so a dialog never hides behind its parent; the keyboard stays
 *     where the window manager put it.
 *   - every POLL_MS: the stand-ins' windows from the window list (content
 *     rectangle, z order, suspended or not) and the keyboard's owner from
 *     the HID routing, published as places (comp_link_place sends only
 *     changes).
 *   - every frame (on the display task, frame_view): when it holds a
 *     fullscreen app's canvas smaller than the screen (426x240 on a 640x400
 *     one), the compositor shows that rectangle alone (L9d, L9e).
 *   - a window asks to be moved or resized (notice, from xdg move/resize):
 *     the stand-in's window is moved / resized through the same calls the
 *     input router makes.
 *   - a window settles on a size of its own (notice FIT, L9b: foot's cell
 *     grid, an X11 program's own size): the stand-in is resized so that its
 *     content is that size over the scale, rounded up -- the window fills
 *     it but for under a frame pixel, which the compositor paints in the
 *     frame's colour.
 *   - a window goes away: the stand-in sees it (FmrbApp.wl_window) and ends.
 *   - the stand-in's close button: FmrbApp.wl_close asks the window to close
 *     (xdg close), so the program can ask about unsaved work first.
 *   - a stand-in that ends while its window is still there (killed): the
 *     window is asked to close, and a new stand-in comes after a while if it
 *     stays.
 *
 * Only started when the program is the compositor (input.backend wlroots).
 * The calls below for the stand-ins (native_wl_*) answer "not a stand-in"
 * everywhere else.
 */

#include <stdio.h>
#include <string.h>

#include "comp_link.h"
#include "fmrb_app.h"
#include "fmrb_err.h"
#include "fmrb_hal.h"
#include "fmrb_hal_time.h"
#include "fmrb_kernel.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"
#include "fmrb_theme.h"
#include "input_source.h"
#include "native_frame.h"
#include "wl_bridge.h"

static const char *TAG = "wl_bridge";

#define POLL_MS 10
/* A stand-in killed while its window stays: the window is asked to close,
 * and if it is still there after this long, a new stand-in comes. */
#define RESPAWN_MS 2000
/* A dialog's stand-in waits this long at most for its parent's. */
#define PARENT_WAIT_MS 1000
/* The frame the stand-in draws around the content (FmrbApp's user area). */
#define FRAME_X 1
#define FRAME_TOP 11
#define FRAME_W 2
#define FRAME_H 12
#define MENU_BAR_H 13

extern const uint8_t linux_window_irep[];

typedef struct {
    int id;                     /* compositor id, 0 = free slot */
    int nat_w, nat_h;           /* the window's size when it appeared */
    char app_id[32];
    char title[COMP_TEXT];
    unsigned title_seq;
    bool alive;                 /* mapped */
    int pid;                    /* the stand-in, -1 none */
    uint32_t gen;               /* its context's generation (slot reuse) */
    uint32_t spawn_at_ms;       /* when to spawn a stand-in (pid -1) */
    int parent;                 /* the parent's compositor id (a dialog), 0 none */
    uint32_t mapped_ms;         /* when the window appeared */
} bw_t;

static struct {
    fmrb_semaphore_t lock;
    bw_t w[COMP_MAX_WINDOWS];
    int spawned;                /* stand-ins started so far (placement cascade) */
} s_br;

static fmrb_window_info_t s_list[FMRB_MAX_APPS];

static bool fit_size(int w, int h, int *ww, int *wh);

static uint32_t ms_now(void)
{
    return (uint32_t)fmrb_hal_time_get_ms();
}

static bw_t *by_id(int id)
{
    for (int i = 0; i < COMP_MAX_WINDOWS; i++) {
        if (s_br.w[i].id == id) return &s_br.w[i];
    }
    return NULL;
}

static bw_t *by_pid(int pid)
{
    if (pid < 0) return NULL;
    for (int i = 0; i < COMP_MAX_WINDOWS; i++) {
        if (s_br.w[i].id != 0 && s_br.w[i].pid == pid) return &s_br.w[i];
    }
    return NULL;
}

/* The stand-in still runs (its slot was not taken by another app since). */
static bool standin_alive(const bw_t *b)
{
    if (b->pid < 0) return false;
    fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id(b->pid);
    if (ctx == NULL || ctx->gen != b->gen) return false;
    return ctx->state == PROC_STATE_INIT || ctx->state == PROC_STATE_RUNNING ||
           ctx->state == PROC_STATE_SUSPENDED;
}

/* {"cmd":"spawned","pid":N} from the new app, as fmrb_app_spawner.c sends
 * for a spawn that did not come through the kernel: the kernel gives it the
 * keyboard and lists it. */
static void tell_kernel_spawned(int pid)
{
    if (pid < 0 || pid > 0x7F) return;
    fmrb_msg_t msg = { .type = FMRB_MSG_TYPE_APP_CONTROL, .src_pid = (fmrb_proc_id_t)pid };
    uint8_t *d = msg.data;
    size_t p = 0;
    d[p++] = 0x82;
    d[p++] = 0xA3;
    memcpy(&d[p], "cmd", 3);
    p += 3;
    d[p++] = 0xA7;
    memcpy(&d[p], "spawned", 7);
    p += 7;
    d[p++] = 0xA3;
    memcpy(&d[p], "pid", 3);
    p += 3;
    d[p++] = (uint8_t)pid;
    msg.size = p;
    fmrb_msg_send(PROC_ID_KERNEL, &msg, 10);
}

/* {"cmd":"focus_app","pid":N}, as the taskbar sends it: the kernel brings
 * the app in front and gives it the keyboard. */
static void tell_kernel_focus(int pid)
{
    if (pid < 0 || pid > 0x7F) return;
    fmrb_msg_t msg = { .type = FMRB_MSG_TYPE_APP_CONTROL, .src_pid = (fmrb_proc_id_t)pid };
    uint8_t *d = msg.data;
    size_t p = 0;
    d[p++] = 0x82;
    d[p++] = 0xA3;
    memcpy(&d[p], "cmd", 3);
    p += 3;
    d[p++] = 0xA9;
    memcpy(&d[p], "focus_app", 9);
    p += 9;
    d[p++] = 0xA3;
    memcpy(&d[p], "pid", 3);
    p += 3;
    d[p++] = (uint8_t)pid;
    msg.size = p;
    fmrb_msg_send(PROC_ID_KERNEL, &msg, 10);
}

/* The window (frame coordinates) of a stand-in, false when it has none. */
static bool standin_window(int pid, fmrb_window_info_t *out)
{
    if (pid < 0) return false;
    int n = fmrb_app_get_window_list(s_list, FMRB_MAX_APPS);
    for (int i = 0; i < n; i++) {
        if (s_list[i].pid == pid) {
            *out = s_list[i];
            return true;
        }
    }
    return false;
}

/* Start a stand-in for the window (called without the lock). parent_pid:
 * the parent's stand-in for a dialog, -1 none. */
static int spawn_standin(int id, int nat_w, int nat_h, const char *app_id, int parent_pid, uint32_t *gen)
{
    int scale = 0, base_w = 0, base_h = 0;
    if (!comp_link_geometry(&scale, &base_w, &base_h)) return -1;
    int ww = 0, wh = 0;
    if (!fit_size(nat_w, nat_h, &ww, &wh)) return -1;
    int x = 0, y = 0;
    fmrb_window_info_t pw;
    bool dialog = standin_window(parent_pid, &pw);
    if (dialog) {
        /* In the middle of the parent's content. */
        comp_dialog_place(pw.x + FRAME_X, pw.y + FRAME_TOP, pw.width - FRAME_W, pw.height - FRAME_H, ww, wh,
                          FRAME_X, FRAME_TOP, FRAME_W, FRAME_H, base_w, base_h, MENU_BAR_H, &x, &y);
    } else {
        int step = (s_br.spawned % 5) * 10;
        x = (base_w - ww) / 2 + step;
        y = MENU_BAR_H + (base_h - MENU_BAR_H - wh) / 2 + step;
        if (x + ww > base_w) x = base_w - ww;
        if (y + wh > base_h) y = base_h - wh;
        if (x < 0) x = 0;
        if (y < MENU_BAR_H) y = MENU_BAR_H;
    }

    char name[FMRB_MAX_APP_NAME];
    snprintf(name, sizeof(name), "%s", app_id[0] ? app_id : "linux");
    fmrb_spawn_attr_t a = {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        .name = name,
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = linux_window_irep,
        .stack_words = FMRB_SHELL_APP_TASK_STACK_SIZE,
        .priority = FMRB_SHELL_APP_PRIORITY,
        .flags = FMRB_SHELL_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        .resizable = true,
        .window_width = (uint16_t)ww,
        .window_height = (uint16_t)wh,
        .window_pos_x = (uint16_t)x,
        .window_pos_y = (uint16_t)y,
        .min_window_width = 64,
        .min_window_height = 40,
        /* Rounded like every window: its canvas is keyed on a colour of
         * its own (native_wl_canvas_key), so the content -- the hole
         * colour, 0x01 -- stays opaque and the corners show through. */
        .rounded_corners = true,
    };
    int32_t pid = -1;
    fmrb_err_t err = fmrb_app_spawn(&a, &pid);
    if (err != FMRB_OK) {
        FMRB_LOGW(TAG, "window %d: no stand-in (spawn error %d)", id, (int)err);
        return -1;
    }
    fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id(pid);
    *gen = ctx ? ctx->gen : 0;
    if (!dialog) s_br.spawned++;
    FMRB_LOGI(TAG, "window %d (%s %dx%d): stand-in pid %d, %dx%d at (%d,%d)%s", id, name, nat_w, nat_h,
              (int)pid, ww, wh, x, y, dialog ? ", in the middle of its parent's" : "");
    tell_kernel_spawned(pid);
    return pid;
}

typedef struct {
    int kind;                   /* 1 move, 2 resize, 3 fit (w, h: output pixels) */
    int pid;
    int x, y, w, h;
} action_t;

static void take_notices(action_t *act, int *n_act)
{
    comp_notice_t nt;
    while (comp_link_take_notice(&nt)) {
        bw_t *b = by_id(nt.id);
        switch (nt.kind) {
        case COMP_NOTICE_MAPPED:
            if (b == NULL) b = by_id(0);
            if (b == NULL) {
                FMRB_LOGW(TAG, "window %d: no room", nt.id);
                break;
            }
            memset(b, 0, sizeof(*b));
            b->id = nt.id;
            b->nat_w = nt.w;
            b->nat_h = nt.h;
            snprintf(b->app_id, sizeof(b->app_id), "%s", nt.text);
            snprintf(b->title, sizeof(b->title), "%s", nt.text);
            b->alive = true;
            b->pid = -1;
            b->parent = nt.x;
            b->mapped_ms = ms_now();
            b->spawn_at_ms = b->mapped_ms;
            break;
        case COMP_NOTICE_PARENT:
            if (b != NULL) b->parent = nt.x;
            break;
        case COMP_NOTICE_TITLE:
            if (b != NULL) {
                snprintf(b->title, sizeof(b->title), "%s", nt.text);
                b->title_seq++;
            }
            break;
        case COMP_NOTICE_UNMAPPED:
            if (b != NULL) b->alive = false;
            break;
        case COMP_NOTICE_MOVE:
            if (b != NULL && b->pid >= 0 && *n_act < COMP_MAX_WINDOWS) {
                act[(*n_act)++] = (action_t){ 1, b->pid, nt.x - FRAME_X, nt.y - FRAME_TOP, 0, 0 };
            }
            break;
        case COMP_NOTICE_RESIZE:
            if (b != NULL && b->pid >= 0 && *n_act < COMP_MAX_WINDOWS) {
                act[(*n_act)++] = (action_t){ 2, b->pid, nt.x - FRAME_X, nt.y - FRAME_TOP, nt.w + FRAME_W,
                                              nt.h + FRAME_H };
            }
            break;
        case COMP_NOTICE_FIT:
            if (b != NULL && *n_act < COMP_MAX_WINDOWS) {
                if (b->pid >= 0) {
                    act[(*n_act)++] = (action_t){ 3, b->pid, 0, 0, nt.w, nt.h };
                } else {
                    /* No stand-in yet: it will be made at this size. */
                    b->nat_w = nt.w;
                    b->nat_h = nt.h;
                }
            }
            break;
        default:
            break;
        }
    }
}

/* The stand-in's window size for a Linux window of w x h output pixels. */
static bool fit_size(int w, int h, int *ww, int *wh)
{
    int scale = 0, base_w = 0, base_h = 0;
    if (!comp_link_geometry(&scale, &base_w, &base_h) || scale <= 0) return false;
    *ww = (w + scale - 1) / scale + FRAME_W;
    *wh = (h + scale - 1) / scale + FRAME_H;
    if (*ww > base_w) *ww = base_w;
    if (*wh > base_h - MENU_BAR_H) *wh = base_h - MENU_BAR_H;
    if (*ww < 64) *ww = 64;
    if (*wh < 40) *wh = 40;
    return true;
}

static void do_action(const action_t *a)
{
    if (a->kind == 3) {
        int ww = 0, wh = 0;
        if (!fit_size(a->w, a->h, &ww, &wh)) return;
        int n = fmrb_app_get_window_list(s_list, FMRB_MAX_APPS);
        for (int i = 0; i < n; i++) {
            if (s_list[i].pid != a->pid) continue;
            if (s_list[i].width != ww || s_list[i].height != wh) {
                FMRB_LOGI(TAG, "stand-in pid %d: %dx%d -> %dx%d to fit its window (%dx%d)", a->pid,
                          s_list[i].width, s_list[i].height, ww, wh, a->w, a->h);
                fmrb_app_update_window_size((uint8_t)a->pid, (uint16_t)ww, (uint16_t)wh);
            }
            break;
        }
        return;
    }
    int x = a->x < 0 ? 0 : a->x;
    int y = a->y < 0 ? 0 : a->y;
    if (a->kind == 2) fmrb_app_update_window_size((uint8_t)a->pid, (uint16_t)a->w, (uint16_t)a->h);
    fmrb_app_update_window_position((uint8_t)a->pid, (uint16_t)x, (uint16_t)y);
}

static void build_places(comp_places_t *pl)
{
    memset(pl, 0, sizeof(*pl));
    int n = fmrb_app_get_window_list(s_list, FMRB_MAX_APPS);
    fmrb_hid_routing_t routing = { .target_pid = 0xFF };
    fmrb_kernel_get_hid_routing(&routing);
    int z[COMP_MAX_WINDOWS];
    for (int i = 0; i < COMP_MAX_WINDOWS; i++) {
        bw_t *b = &s_br.w[i];
        if (b->id == 0 || !b->alive || b->pid < 0) continue;
        for (int j = 0; j < n; j++) {
            if (s_list[j].pid != b->pid) continue;
            fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id(b->pid);
            comp_place_t p;
            memset(&p, 0, sizeof(p));
            p.id = b->id;
            p.x = s_list[j].x + FRAME_X;
            p.y = s_list[j].y + FRAME_TOP;
            p.w = s_list[j].width - FRAME_W;
            p.h = s_list[j].height - FRAME_H;
            p.shown = ctx != NULL && ctx->state == PROC_STATE_RUNNING;
            /* Insert by z (bottom to top). */
            int k = pl->n;
            while (k > 0 && z[k - 1] > s_list[j].z_order) {
                pl->p[k] = pl->p[k - 1];
                z[k] = z[k - 1];
                k--;
            }
            pl->p[k] = p;
            z[k] = s_list[j].z_order;
            pl->n++;
            if (routing.routing_enabled && routing.target_pid == b->pid) pl->focus = b->id;
            break;
        }
    }
}

/* The view (L9d, L9e): a fullscreen app's canvas smaller than the screen
 * (Modern's 426x240, or 640x360 for an app laid out for it, on the
 * compositor's wider screen) is shown alone, at its own largest whole scale.
 * Decided on the display task from the canvases each frame was made of, so
 * the view changes with the very frame that has the app (or no longer has
 * it): never a frame shown the old way. The topmost such canvas; any canvas
 * over it lies inside it or is cut. */
static bool frame_view(const native_frame_canvas_t *c, int n, int fb_w, int fb_h, int view[4])
{
    int base_w = 0, base_h = 0;
    native_frame_base_size(&base_w, &base_h);
    if (fb_w != base_w || fb_h != base_h) return false;
    for (int i = n - 1; i >= 0; i--) {
        if (!fmrb_app_canvas_fullscreen(c[i].canvas_id)) continue;
        int x0 = c[i].x < 0 ? 0 : c[i].x, y0 = c[i].y < 0 ? 0 : c[i].y;
        int x1 = c[i].x + c[i].w > fb_w ? fb_w : c[i].x + c[i].w;
        int y1 = c[i].y + c[i].h > fb_h ? fb_h : c[i].y + c[i].h;
        if (x1 - x0 <= 0 || y1 - y0 <= 0 || (x1 - x0 >= fb_w && y1 - y0 >= fb_h)) return false;
        view[0] = x0;
        view[1] = y0;
        view[2] = x1 - x0;
        view[3] = y1 - y0;
        return true;
    }
    return false;
}

/* A dialog's stand-in in front of its parent's whenever the parent's is above
 * it (one raise per pass; chains of dialogs take a pass per link), and the
 * keyboard with it (L9e): the kernel is asked to focus the dialog as the
 * taskbar would. That request is handled after whatever raised the parent
 * (a click gives the parent the keyboard in the same handling), so the
 * dialog ends with it; the compositor follows the focus with the next
 * places. Asked once per FOCUS_ASK_MS for a dialog. Without the lock: the
 * pids are copied first. */
#define FOCUS_ASK_MS 300
static void keep_dialogs_above(void)
{
    static int asked_pid = -1;
    static uint32_t asked_ms;
    int pairs[COMP_MAX_WINDOWS][2];
    int np = 0;
    fmrb_semaphore_take(s_br.lock, FMRB_TICK_MAX);
    for (int i = 0; i < COMP_MAX_WINDOWS; i++) {
        bw_t *b = &s_br.w[i];
        if (b->id == 0 || b->pid < 0 || b->parent <= 0 || !b->alive) continue;
        bw_t *pb = by_id(b->parent);
        if (pb == NULL || pb->pid < 0) continue;
        pairs[np][0] = b->pid;
        pairs[np][1] = pb->pid;
        np++;
    }
    fmrb_semaphore_give(s_br.lock);
    for (int pass = 0; pass < np; pass++) {
        int n = fmrb_app_get_window_list(s_list, FMRB_MAX_APPS);
        bool raised = false;
        for (int k = 0; k < np && !raised; k++) {
            int zd = -1, zp = -1;
            for (int i = 0; i < n; i++) {
                if (s_list[i].pid == pairs[k][0]) zd = s_list[i].z_order;
                if (s_list[i].pid == pairs[k][1]) zp = s_list[i].z_order;
            }
            if (zd < 0 || zp < 0 || zd > zp) continue;
            uint32_t t = ms_now();
            if (asked_pid != pairs[k][0] || t - asked_ms >= FOCUS_ASK_MS) {
                asked_pid = pairs[k][0];
                asked_ms = t;
                tell_kernel_focus(pairs[k][0]);
            }
            if (fmrb_app_bring_to_front((uint8_t)pairs[k][0]) == FMRB_OK) {
                FMRB_LOGI(TAG, "stand-in pid %d in front of its parent's (pid %d), the keyboard asked for", pairs[k][0],
                          pairs[k][1]);
                raised = true;
            }
        }
        if (!raised) break;
    }
}

static void bridge_task(void *arg)
{
    (void)arg;
    /* The desktop first: a stand-in before it would be laid out before the
     * screen is. The input sources start before the app table exists, so
     * nothing is asked of it until the display has shown a frame (there is
     * no frame without an app's canvas). */
    for (;;) {
        bool up = comp_link_attached() && native_frame_seq() > 0;
        int n = up ? fmrb_app_get_window_list(s_list, FMRB_MAX_APPS) : 0;
        bool desktop = false;
        for (int i = 0; i < n; i++) {
            if (strcmp(s_list[i].app_name, "system_desktop") == 0) desktop = true;
        }
        if (desktop) break;
        fmrb_task_delay_ms(100);
    }
    FMRB_LOGI(TAG, "Linux windows are Family mruby windows from now on");
    for (;;) {
        action_t act[COMP_MAX_WINDOWS];
        int n_act = 0;
        struct {
            int id, w, h, parent_pid;
            char app_id[32];
        } want[COMP_MAX_WINDOWS];
        int n_want = 0;
        int ask_close[COMP_MAX_WINDOWS];
        int n_close = 0;

        fmrb_semaphore_take(s_br.lock, FMRB_TICK_MAX);
        take_notices(act, &n_act);
        uint32_t t = ms_now();
        for (int i = 0; i < COMP_MAX_WINDOWS; i++) {
            bw_t *b = &s_br.w[i];
            if (b->id == 0) continue;
            if (b->pid >= 0 && !standin_alive(b)) {
                FMRB_LOGI(TAG, "window %d: stand-in pid %d ended%s", b->id, b->pid,
                          b->alive ? " with the window still there: asking it to close" : "");
                b->pid = -1;
                if (b->alive) {
                    ask_close[n_close++] = b->id;
                    b->spawn_at_ms = t + RESPAWN_MS;
                }
            }
            if (!b->alive && b->pid < 0) {
                memset(b, 0, sizeof(*b));
                continue;
            }
            if (b->alive && b->pid < 0 && (int32_t)(t - b->spawn_at_ms) >= 0) {
                /* A dialog waits a moment for its parent's stand-in. */
                bw_t *pb = b->parent > 0 ? by_id(b->parent) : NULL;
                if (pb != NULL && pb->alive && pb->pid < 0 && (int32_t)(t - b->mapped_ms) < PARENT_WAIT_MS) continue;
                want[n_want].parent_pid = pb != NULL ? pb->pid : -1;
                want[n_want].id = b->id;
                want[n_want].w = b->nat_w;
                want[n_want].h = b->nat_h;
                snprintf(want[n_want].app_id, sizeof(want[n_want].app_id), "%s", b->app_id);
                n_want++;
                b->spawn_at_ms = t + RESPAWN_MS;
            }
        }
        fmrb_semaphore_give(s_br.lock);

        for (int i = 0; i < n_close; i++) comp_link_command(COMP_CMD_CLOSE, ask_close[i]);
        for (int i = 0; i < n_act; i++) do_action(&act[i]);
        for (int i = 0; i < n_want; i++) {
            uint32_t gen = 0;
            int pid = spawn_standin(want[i].id, want[i].w, want[i].h, want[i].app_id, want[i].parent_pid, &gen);
            if (pid < 0) continue;
            fmrb_semaphore_take(s_br.lock, FMRB_TICK_MAX);
            bw_t *b = by_id(want[i].id);
            if (b != NULL) {
                b->pid = pid;
                b->gen = gen;
            }
            fmrb_semaphore_give(s_br.lock);
        }

        keep_dialogs_above();

        comp_places_t pl;
        fmrb_semaphore_take(s_br.lock, FMRB_TICK_MAX);
        build_places(&pl);
        fmrb_semaphore_give(s_br.lock);
        comp_link_place(&pl);
        fmrb_task_delay_ms(POLL_MS);
    }
}

static fmrb_err_t bridge_start(void)
{
    native_frame_set_view_fn(frame_view);
    s_br.lock = fmrb_semaphore_create_mutex();
    if (s_br.lock == NULL) return FMRB_ERR_NO_MEMORY;
    if (fmrb_task_create(bridge_task, "wl_bridge", 16384, NULL, 5, NULL) != FMRB_PASS) {
        FMRB_LOGE(TAG, "failed to create the compositor windows task");
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

const fmrb_input_source_t fmrb_input_source_wl_bridge = {
    .name  = "compositor windows",
    .start = bridge_start,
};

/* ---- the stand-ins' calls (FmrbApp.wl_window / wl_close / wl_key) ------- */

/* The stand-in's canvas key (L9d): a colour its frame never draws -- not the
 * hole (0x01), nor the theme's title bar, text and border it is drawn in --
 * the first of a few dark ones that is free. Every other canvas keeps key.
 * Asked by the canvas's creation (fmrb_app_canvas.c) and by the stand-in,
 * which paints it outside its rounded corners. */
uint8_t native_wl_canvas_key(const fmrb_app_task_context_t *ctx, uint8_t key)
{
    if (ctx == NULL || ctx->load_mode != FMRB_LOAD_MODE_BYTECODE || ctx->load_data != (const void *)linux_window_irep) {
        return key;
    }
    static const uint8_t candidates[] = { 0x02, 0x03, 0x05, 0x06, 0x07 };
    const fmrb_theme_t *t = fmrb_theme_get();
    for (size_t i = 0; i < sizeof(candidates); i++) {
        uint8_t c = candidates[i];
        if (c == key || c == 0x01) continue;
        if (t != NULL && (c == t->menu_bg || c == t->text_light || c == t->border)) continue;
        return c;
    }
    return key;
}

int native_wl_window_state(int pid, char *title, size_t cap)
{
    if (s_br.lock == NULL) return -1;
    fmrb_semaphore_take(s_br.lock, FMRB_TICK_MAX);
    bw_t *b = by_pid(pid);
    int st = -1;
    if (b != NULL) {
        st = b->alive ? 1 : 0;
        if (title != NULL && cap > 0) snprintf(title, cap, "%s", b->title);
    }
    fmrb_semaphore_give(s_br.lock);
    return st;
}

bool native_wl_window_close(int pid)
{
    if (s_br.lock == NULL) return false;
    fmrb_semaphore_take(s_br.lock, FMRB_TICK_MAX);
    bw_t *b = by_pid(pid);
    int id = (b != NULL && b->alive) ? b->id : 0;
    fmrb_semaphore_give(s_br.lock);
    if (id == 0) return false;
    FMRB_LOGI(TAG, "window %d: asked to close by its stand-in (pid %d)", id, pid);
    comp_link_command(COMP_CMD_CLOSE, id);
    return true;
}
