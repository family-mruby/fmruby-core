/*
 * native_winops.c - see native_winops.h.
 */

#include "native_winops.h"

#include <string.h>

#include "fmrb_app.h"
#include "fmrb_hal_time.h"
#include "fmrb_kernel.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include "native_frame.h"

static const char *TAG = "winops";

#define HID_RIGHT 0x4F
#define HID_LEFT 0x50
#define HID_DOWN 0x51
#define HID_UP 0x52
#define HID_LGUI 0xE3
#define HID_RGUI 0xE7

typedef struct {
    uint32_t gen;               /* the app's context generation (slot reuse) */
    winops_layout_t layout;
    winops_rect_t saved;        /* before the first operation */
    winops_rect_t set;          /* what the last operation left */
} slot_t;

static struct {
    bool enabled;
    fmrb_semaphore_t lock;
    slot_t slot[FMRB_MAX_APPS];
    /* The last press on a title: a second one near it soon is a double-click. */
    int title_pid;
    unsigned title_ms;
    int title_x, title_y;
    bool swallow_up;            /* the double-click's release */
    uint8_t gui_held;           /* bit 0 left Super, bit 1 right */
    uint8_t arrows_taken;       /* the arrows whose press was taken (bit per arrow) */
} s_wo = { .title_pid = -1 };

static fmrb_window_info_t s_list[FMRB_MAX_APPS];

void native_winops_setup(bool enabled)
{
    s_wo.enabled = enabled;
}

bool native_winops_enabled(void)
{
    return s_wo.enabled;
}

/* The app table exists once a frame has been shown (the input sources start
 * before it does). */
static bool ready(void)
{
    if (!s_wo.enabled || native_frame_seq() == 0) return false;
    /* Made on the first call, from a task (the scheduler runs by then). */
    if (s_wo.lock == NULL) s_wo.lock = fmrb_semaphore_create_mutex();
    return s_wo.lock != NULL;
}

static bool rect_eq(winops_rect_t a, winops_rect_t b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

/* The window of pid (s_list is filled), NULL when it has none. */
static const fmrb_window_info_t *window_of(int pid, int n)
{
    for (int i = 0; i < n; i++) {
        if (s_list[i].pid == pid) return &s_list[i];
    }
    return NULL;
}

static winops_rect_t rect_of(const fmrb_window_info_t *w)
{
    return (winops_rect_t){ w->x, w->y, w->width, w->height };
}

static slot_t *slot_of(int pid)
{
    if (pid < 0 || pid >= FMRB_MAX_APPS) return NULL;
    fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id(pid);
    if (ctx == NULL) return NULL;
    slot_t *sl = &s_wo.slot[pid];
    if (sl->gen != ctx->gen) {
        memset(sl, 0, sizeof(*sl));
        sl->gen = ctx->gen;
    }
    return sl;
}

static const char *layout_name(winops_layout_t l)
{
    switch (l) {
    case WINOPS_MAX: return "maximized";
    case WINOPS_LEFT: return "on the left half";
    case WINOPS_RIGHT: return "on the right half";
    default: return "back";
    }
}

bool native_winops_apply(int pid, winops_do_t op, const char *why)
{
    if (!ready() || pid < 0 || pid >= FMRB_MAX_APPS) return false;
    int sw = 0, sh = 0;
    native_frame_base_size(&sw, &sh);
    if (sw <= 0 || sh <= 0) return false;
    fmrb_semaphore_take(s_wo.lock, FMRB_TICK_MAX);
    int n = fmrb_app_get_window_list(s_list, FMRB_MAX_APPS);
    const fmrb_window_info_t *w = window_of(pid, n);
    slot_t *sl = slot_of(pid);
    if (w == NULL || sl == NULL || !w->resizable || w->fullscreen || strcmp(w->app_name, "system_desktop") == 0) {
        fmrb_semaphore_give(s_wo.lock);
        return false;
    }
    winops_rect_t cur = rect_of(w);
    /* Moved or resized by the user since: as the user left it. */
    if (sl->layout != WINOPS_NONE && !rect_eq(cur, sl->set)) sl->layout = WINOPS_NONE;
    if (op == WINOPS_DO_TOGGLE) op = sl->layout == WINOPS_MAX ? WINOPS_DO_RESTORE : WINOPS_DO_MAX;
    winops_layout_t want = op == WINOPS_DO_MAX ? WINOPS_MAX
                         : op == WINOPS_DO_LEFT ? WINOPS_LEFT
                         : op == WINOPS_DO_RIGHT ? WINOPS_RIGHT
                         : WINOPS_NONE;
    if (want == sl->layout) {
        fmrb_semaphore_give(s_wo.lock);
        return false;
    }
    winops_rect_t to;
    if (want == WINOPS_NONE) {
        to = sl->saved;
    } else {
        if (sl->layout == WINOPS_NONE) sl->saved = cur;
        to = winops_layout_rect(want, sw, sh);
    }
    fmrb_app_update_window_size((uint8_t)pid, (uint16_t)to.w, (uint16_t)to.h);
    fmrb_app_update_window_position((uint8_t)pid, (uint16_t)(to.x < 0 ? 0 : to.x), (uint16_t)(to.y < 0 ? 0 : to.y));
    n = fmrb_app_get_window_list(s_list, FMRB_MAX_APPS);
    w = window_of(pid, n);
    sl->set = w != NULL ? rect_of(w) : to;
    sl->layout = want;
    FMRB_LOGI(TAG, "PID %d '%s' %s: %dx%d at (%d,%d) (%s)", pid, w ? w->app_name : "?", layout_name(want), sl->set.w,
              sl->set.h, sl->set.x, sl->set.y, why ? why : "");
    fmrb_semaphore_give(s_wo.lock);
    return true;
}

bool native_winops_front(winops_do_t op, const char *why)
{
    if (!ready()) return false;
    fmrb_hid_routing_t routing = { .target_pid = 0xFF };
    if (fmrb_kernel_get_hid_routing(&routing) != FMRB_OK || !routing.routing_enabled || routing.target_pid == 0xFF) {
        return false;
    }
    return native_winops_apply(routing.target_pid, op, why);
}

winops_layout_t native_winops_layout(int pid)
{
    if (!ready() || pid < 0 || pid >= FMRB_MAX_APPS) return WINOPS_NONE;
    fmrb_semaphore_take(s_wo.lock, FMRB_TICK_MAX);
    winops_layout_t l = WINOPS_NONE;
    slot_t *sl = slot_of(pid);
    if (sl != NULL && sl->layout != WINOPS_NONE) {
        int n = fmrb_app_get_window_list(s_list, FMRB_MAX_APPS);
        const fmrb_window_info_t *w = window_of(pid, n);
        if (w != NULL && rect_eq(rect_of(w), sl->set)) l = sl->layout;
    }
    fmrb_semaphore_give(s_wo.lock);
    return l;
}

/* The window whose title is at (x, y): the topmost running window there,
 * as the window manager finds it (the desktop and fullscreen apps have no
 * title). -1 when the point is on none. */
static int title_at(int x, int y)
{
    if (y < WINOPS_MENU_BAR_H) return -1;
    int n = fmrb_app_get_window_list(s_list, FMRB_MAX_APPS);
    const fmrb_window_info_t *top = NULL;
    for (int i = 0; i < n; i++) {
        const fmrb_window_info_t *w = &s_list[i];
        if (strcmp(w->app_name, "system_desktop") == 0) continue;
        fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id(w->pid);
        if (ctx == NULL || ctx->state != PROC_STATE_RUNNING) continue;
        if (x < w->x || y < w->y || x >= w->x + w->width || y >= w->y + w->height) continue;
        if (top == NULL || w->z_order > top->z_order) top = w;
    }
    if (top == NULL || top->fullscreen || !top->resizable) return -1;
    return winops_on_title(x - top->x, y - top->y, top->width) ? top->pid : -1;
}

bool native_winops_button(int x, int y, int button, bool down)
{
    if (!ready() || button != 1) return false;
    if (!down) {
        if (!s_wo.swallow_up) return false;
        s_wo.swallow_up = false;
        return true;
    }
    unsigned t = (unsigned)fmrb_hal_time_get_ms();
    int pid = title_at(x, y);
    if (pid >= 0 && pid == s_wo.title_pid && winops_double(s_wo.title_ms, s_wo.title_x, s_wo.title_y, t, x, y)) {
        s_wo.title_pid = -1;
        s_wo.swallow_up = true;
        native_winops_apply(pid, WINOPS_DO_TOGGLE, "a double-click on its title");
        return true;
    }
    s_wo.title_pid = pid;
    s_wo.title_ms = t;
    s_wo.title_x = x;
    s_wo.title_y = y;
    return false;
}

winops_do_t native_winops_arrow(uint8_t usage)
{
    switch (usage) {
    case HID_LEFT: return WINOPS_DO_LEFT;
    case HID_RIGHT: return WINOPS_DO_RIGHT;
    case HID_UP: return WINOPS_DO_MAX;
    case HID_DOWN: return WINOPS_DO_RESTORE;
    default: return (winops_do_t)0;
    }
}

bool native_winops_key(uint8_t usage, bool down)
{
    if (!s_wo.enabled) return false;
    if (usage == HID_LGUI || usage == HID_RGUI) {
        uint8_t bit = usage == HID_LGUI ? 1 : 2;
        if (down) s_wo.gui_held |= bit;
        else s_wo.gui_held &= (uint8_t)~bit;
        return false;
    }
    winops_do_t op = native_winops_arrow(usage);
    if (op == 0) return false;
    uint8_t abit = (uint8_t)(1u << (usage - HID_RIGHT));
    if (!down) {
        if (!(s_wo.arrows_taken & abit)) return false;
        s_wo.arrows_taken &= (uint8_t)~abit;
        return true;
    }
    if (s_wo.gui_held == 0) return false;
    s_wo.arrows_taken |= abit;
    native_winops_front(op, "Super and an arrow");
    return true;
}
