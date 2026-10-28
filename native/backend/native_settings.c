/*
 * native_settings.c - the PC's settings window (doc/linux_native, L14):
 * Wi-Fi and Bluetooth, an app built into the native program
 * (native/apps/linux_settings.app.rb, its bytecode compiled by
 * rake native:mrb), opened from the desktop's network dialog ("Wi-Fi...")
 * and its menu ("Bluetooth"). No device, simulation or browser build has
 * it.
 *
 * One window at most: asking again brings it in front on the page asked
 * for (the app reads the page and a count of the asks, FmrbApp.settings_page).
 * Called from the desktop's task (FmrbApp.linux_settings).
 */

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "fmrb_app.h"
#include "fmrb_err.h"
#include "fmrb_kernel.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"
#include "fmrb_task_config.h"
#include "native_frame.h"

static const char *TAG = "settings";

extern const uint8_t linux_settings_irep[];

#define WIN_W 280
#define WIN_H 200
#define MENU_BAR_H 13

static atomic_int s_asks;      /* page in the low bit, the count above */
static int s_pid = -1;
static uint32_t s_gen;

/* {"cmd":<cmd>,"pid":N} to the kernel, as the taskbar sends them. */
static void tell_kernel(const char *cmd, int pid)
{
    if (pid < 0 || pid > 0x7F) return;
    size_t cl = strlen(cmd);
    if (cl > 31) return;
    fmrb_msg_t msg = { .type = FMRB_MSG_TYPE_APP_CONTROL, .src_pid = (fmrb_proc_id_t)pid };
    uint8_t *d = msg.data;
    size_t p = 0;
    d[p++] = 0x82;
    d[p++] = 0xA3;
    memcpy(&d[p], "cmd", 3);
    p += 3;
    d[p++] = (uint8_t)(0xA0 | cl);
    memcpy(&d[p], cmd, cl);
    p += cl;
    d[p++] = 0xA3;
    memcpy(&d[p], "pid", 3);
    p += 3;
    d[p++] = (uint8_t)pid;
    msg.size = p;
    fmrb_msg_send(PROC_ID_KERNEL, &msg, 10);
}

static bool still_open(void)
{
    if (s_pid < 0) return false;
    fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id(s_pid);
    return ctx != NULL && ctx->gen == s_gen && ctx->load_mode == FMRB_LOAD_MODE_BYTECODE &&
           ctx->load_data == (const void *)linux_settings_irep;
}

bool native_settings_open(const char *page)
{
    int p = (page != NULL && strcmp(page, "bluetooth") == 0) ? 1 : 0;
    int prev = atomic_load(&s_asks);
    atomic_store(&s_asks, ((prev >> 1) + 1) << 1 | p);
    if (still_open()) {
        FMRB_LOGI(TAG, "the settings window (pid %d) to the %s page", s_pid, p ? "Bluetooth" : "Wi-Fi");
        tell_kernel("focus_app", s_pid);
        return true;
    }
    int bw = 0, bh = 0;
    native_frame_base_size(&bw, &bh);
    if (bw <= 0 || bh <= 0) {
        bw = 426;
        bh = 240;
    }
    int ww = WIN_W < bw ? WIN_W : bw;
    int wh = WIN_H < bh - MENU_BAR_H ? WIN_H : bh - MENU_BAR_H;
    int x = (bw - ww) / 2;
    int y = MENU_BAR_H + (bh - MENU_BAR_H - wh) / 2;
    const fmrb_system_config_t *cfg = fmrb_kernel_get_config();
    bool ja = cfg != NULL && strcmp(cfg->language, "ja") == 0;
    char name[FMRB_MAX_APP_NAME];
    snprintf(name, sizeof(name), "%s", ja ? "設定" : "Settings");
    fmrb_spawn_attr_t a = {
        .app_id = -1,
        .type = APP_TYPE_USER_APP,
        .name = name,
        .vm_type = FMRB_VM_TYPE_MRUBY,
        .load_mode = FMRB_LOAD_MODE_BYTECODE,
        .bytecode = linux_settings_irep,
        .stack_words = FMRB_SHELL_APP_TASK_STACK_SIZE,
        .priority = FMRB_SHELL_APP_PRIORITY,
        .flags = FMRB_SHELL_APP_TASK_FLAGS,
        .core_affinity = -1,
        .headless = false,
        .resizable = false,
        .window_width = (uint16_t)ww,
        .window_height = (uint16_t)wh,
        .window_pos_x = (uint16_t)x,
        .window_pos_y = (uint16_t)y,
        .rounded_corners = true,
    };
    int32_t pid = -1;
    fmrb_err_t err = fmrb_app_spawn(&a, &pid);
    if (err != FMRB_OK) {
        FMRB_LOGW(TAG, "the settings window did not open (spawn error %d)", (int)err);
        return false;
    }
    fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id(pid);
    s_pid = pid;
    s_gen = ctx ? ctx->gen : 0;
    FMRB_LOGI(TAG, "the settings window: pid %d, %dx%d at (%d,%d), the %s page", (int)pid, ww, wh, x, y,
              p ? "Bluetooth" : "Wi-Fi");
    tell_kernel("spawned", pid);
    return true;
}

int native_settings_page(void)
{
    return atomic_load(&s_asks);
}
