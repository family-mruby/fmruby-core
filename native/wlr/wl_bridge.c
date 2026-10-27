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
 *     keyboard.
 *   - every POLL_MS: the stand-ins' windows from the window list (content
 *     rectangle, z order, suspended or not) and the keyboard's owner from
 *     the HID routing, published as places (comp_link_place sends only
 *     changes).
 *   - a window asks to be moved or resized (notice, from xdg move/resize):
 *     the stand-in's window is moved / resized through the same calls the
 *     input router makes.
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
#include "input_source.h"
#include "native_frame.h"
#include "wl_bridge.h"

static const char *TAG = "wl_bridge";

#define POLL_MS 10
/* A stand-in killed while its window stays: the window is asked to close,
 * and if it is still there after this long, a new stand-in comes. */
#define RESPAWN_MS 2000
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
} bw_t;

static struct {
    fmrb_semaphore_t lock;
    bw_t w[COMP_MAX_WINDOWS];
    int spawned;                /* stand-ins started so far (placement cascade) */
} s_br;

static fmrb_window_info_t s_list[FMRB_MAX_APPS];

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

/* Start a stand-in for the window (called without the lock). */
static int spawn_standin(int id, int nat_w, int nat_h, const char *app_id, uint32_t *gen)
{
    int scale = 0, base_w = 0, base_h = 0;
    if (!comp_link_geometry(&scale, &base_w, &base_h)) return -1;
    int cw = (nat_w + scale - 1) / scale;
    int ch = (nat_h + scale - 1) / scale;
    int ww = cw + FRAME_W;
    int wh = ch + FRAME_H;
    if (ww > base_w) ww = base_w;
    if (wh > base_h - MENU_BAR_H) wh = base_h - MENU_BAR_H;
    if (ww < 64) ww = 64;
    if (wh < 40) wh = 40;
    int step = (s_br.spawned % 5) * 10;
    int x = (base_w - ww) / 2 + step;
    int y = MENU_BAR_H + (base_h - MENU_BAR_H - wh) / 2 + step;
    if (x + ww > base_w) x = base_w - ww;
    if (y + wh > base_h) y = base_h - wh;
    if (x < 0) x = 0;
    if (y < MENU_BAR_H) y = MENU_BAR_H;

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
        /* The content is the hole colour, which a colour-keyed canvas would
         * let through to the desktop; this canvas composites opaque. */
        .rounded_corners = false,
    };
    int32_t pid = -1;
    fmrb_err_t err = fmrb_app_spawn(&a, &pid);
    if (err != FMRB_OK) {
        FMRB_LOGW(TAG, "window %d: no stand-in (spawn error %d)", id, (int)err);
        return -1;
    }
    fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id(pid);
    *gen = ctx ? ctx->gen : 0;
    s_br.spawned++;
    FMRB_LOGI(TAG, "window %d (%s %dx%d): stand-in pid %d, %dx%d at (%d,%d)", id, name, nat_w, nat_h,
              (int)pid, ww, wh, x, y);
    tell_kernel_spawned(pid);
    return pid;
}

typedef struct {
    int kind;                   /* 1 move, 2 resize */
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
            b->spawn_at_ms = ms_now();
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
        default:
            break;
        }
    }
}

static void do_action(const action_t *a)
{
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
            int id, w, h;
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
            int pid = spawn_standin(want[i].id, want[i].w, want[i].h, want[i].app_id, &gen);
            if (pid < 0) continue;
            fmrb_semaphore_take(s_br.lock, FMRB_TICK_MAX);
            bw_t *b = by_id(want[i].id);
            if (b != NULL) {
                b->pid = pid;
                b->gen = gen;
            }
            fmrb_semaphore_give(s_br.lock);
        }

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

/* ---- the stand-ins' calls (FmrbApp.wl_window / wl_close) ---------------- */

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
