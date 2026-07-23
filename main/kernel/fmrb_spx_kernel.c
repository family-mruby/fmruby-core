/**
 * @file fmrb_spx_kernel.c
 * @brief Implementation of the Spinel FFI shim (fmrb_spx.h).
 *
 * These functions wrap the same fmrb_* operations the mruby kernel binding
 * (lib/add/picoruby-fmrb-kernel/ports/esp32/kernel.c) uses, but with a plain C
 * ABI callable from Spinel-generated code. See fmrb_spx.h for the contract.
 */
#include "fmrb_spx.h"

#include <string.h>
#include <time.h>
#include "fmrb_app.h"
#include "fmrb_kernel.h"
#include "fmrb_msg.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"
#include "fmrb_log.h"

static const char *TAG = "spx";

uint32_t fmrb_spx_board_millis(void)
{
    /* CLOCK_MONOTONIC milliseconds. Portable across the IDF Linux target and
       the ESP32 (esp-idf provides POSIX clock_gettime), avoiding an esp_timer
       component dependency. */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

void fmrb_spx_log_write(int level, const char *msg, int len)
{
    if (!msg || len < 0) {
        return;
    }
    switch (level) {
        case 0:  FMRB_LOGD(TAG, "%.*s", len, msg); break;
        case 2:  FMRB_LOGW(TAG, "%.*s", len, msg); break;
        case 3:  FMRB_LOGE(TAG, "%.*s", len, msg); break;
        default: FMRB_LOGI(TAG, "%.*s", len, msg); break;
    }
}

/* Byte length for Spinel's :binstr FFI return. The runtime's codegen emits
   `extern int sp_net_bin_len` for any :binstr callsite and builds the string
   with sp_str_from_bytes(ptr, sp_net_bin_len). sp_net.c (which normally owns
   this global) is excluded from the fmruby runtime snapshot, so provide it
   here. TODO (fork follow-up): generalize sp_net_bin_len -> sp_ffi_bin_len in a
   core runtime file so any ffi_func can publish a binary length (upstream PR). */
int sp_net_bin_len = 0;

const char *fmrb_spx_recv_message(int timeout_ms, int *type, int *src_pid)
{
    static uint8_t payload[FMRB_MAX_MSG_PAYLOAD_SIZE];
    if (type) *type = -1;
    if (src_pid) *src_pid = -1;
    sp_net_bin_len = 0;

    fmrb_msg_t msg;
    /* fmrb_msg_receive takes milliseconds (it applies FMRB_MS_TO_TICKS
       internally); pass timeout_ms straight through. */
    fmrb_err_t ret = fmrb_msg_receive(PROC_ID_KERNEL, &msg,
                                      (uint32_t)(timeout_ms < 0 ? 0 : timeout_ms));
    if (ret != FMRB_OK) {
        return "";  /* timeout / error: empty payload, type stays -1 */
    }
    uint32_t n = msg.size;
    if (n > sizeof(payload)) n = sizeof(payload);
    memcpy(payload, msg.data, n);
    if (type) *type = (int)msg.type;
    if (src_pid) *src_pid = (int)msg.src_pid;
    sp_net_bin_len = (int)n;
    return (const char *)payload;
}

static int spx_send_impl(int dst_pid, int type, const uint8_t *data, int len,
                         uint32_t timeout_ms)
{
    if (!data || len < 0 || len > FMRB_MAX_MSG_PAYLOAD_SIZE) {
        return FMRB_SPX_ERR_RANGE;
    }
    if (dst_pid < 0 || dst_pid > 255) {
        return FMRB_SPX_ERR_RANGE;
    }
    fmrb_msg_t msg = {
        .type = (fmrb_msg_type_t)type,
        .src_pid = PROC_ID_KERNEL,
        .size = (uint32_t)len,
    };
    memcpy(msg.data, data, (size_t)len);
    fmrb_err_t ret = fmrb_msg_send((uint8_t)dst_pid, &msg, timeout_ms);
    return ret == FMRB_OK ? 1 : 0;
}

int fmrb_spx_send_raw(int dst_pid, int type, const uint8_t *data, int len)
{
    return spx_send_impl(dst_pid, type, data, len, 100);
}

int fmrb_spx_try_send_raw(int dst_pid, int type, const uint8_t *data, int len)
{
    return spx_send_impl(dst_pid, type, data, len, 0);
}

int fmrb_spx_windows_snapshot(uint8_t *buf, int cap)
{
    if (!buf || cap < 0) {
        return FMRB_SPX_ERR;
    }
    fmrb_window_info_t windows[FMRB_MAX_APPS];
    int32_t count = fmrb_app_get_window_list(windows, FMRB_MAX_APPS);
    if (count < 0) {
        return FMRB_SPX_ERR;
    }
    if (count * FMRB_SPX_WIN_RECORD_SIZE > cap) {
        return FMRB_SPX_ERR_CAP;
    }
    for (int32_t i = 0; i < count; i++) {
        uint8_t *r = buf + (size_t)i * FMRB_SPX_WIN_RECORD_SIZE;
        const fmrb_window_info_t *w = &windows[i];
        memset(r, 0, FMRB_SPX_WIN_RECORD_SIZE);
        r[0] = w->pid;
        r[1] = w->z_order;
        r[2] = (uint8_t)((w->fullscreen ? FMRB_SPX_WIN_FLAG_FULLSCREEN : 0) |
                         (w->resizable ? FMRB_SPX_WIN_FLAG_RESIZABLE : 0));
        r[3] = 0;
        r[4] = (uint8_t)(w->x & 0xFF);          r[5] = (uint8_t)(w->x >> 8);
        r[6] = (uint8_t)(w->y & 0xFF);          r[7] = (uint8_t)(w->y >> 8);
        r[8] = (uint8_t)(w->width & 0xFF);      r[9] = (uint8_t)(w->width >> 8);
        r[10] = (uint8_t)(w->height & 0xFF);    r[11] = (uint8_t)(w->height >> 8);
        r[12] = (uint8_t)(w->min_width & 0xFF); r[13] = (uint8_t)(w->min_width >> 8);
        r[14] = (uint8_t)(w->min_height & 0xFF);r[15] = (uint8_t)(w->min_height >> 8);
        size_t nl = strnlen(w->app_name, 32);
        memcpy(r + 16, w->app_name, nl);
    }
    return (int)count;
}

int fmrb_spx_set_hid_target(int pid)
{
    if (pid < 0 || pid > 255) {
        return FMRB_SPX_ERR_RANGE;
    }
    return fmrb_kernel_set_hid_target((uint8_t)pid) == FMRB_OK ? 0 : FMRB_SPX_ERR;
}

int fmrb_spx_set_focused_window(int win_id)
{
    if (win_id < 0 || win_id > 255) {
        return FMRB_SPX_ERR_RANGE;
    }
    return fmrb_kernel_set_focused_window((uint8_t)win_id) == FMRB_OK ? 0 : FMRB_SPX_ERR;
}

int fmrb_spx_bring_to_front(int pid)
{
    if (pid < 0 || pid > 255) {
        return FMRB_SPX_ERR_RANGE;
    }
    return fmrb_app_bring_to_front((uint8_t)pid) == FMRB_OK ? 1 : 0;
}

int fmrb_spx_update_window_pos(int pid, int x, int y)
{
    if (pid < 0 || pid > 255 || x < 0 || x > 65535 || y < 0 || y > 65535) {
        return FMRB_SPX_ERR_RANGE;
    }
    return fmrb_app_update_window_position((uint8_t)pid, (uint16_t)x, (uint16_t)y) == FMRB_OK ? 1 : 0;
}

int fmrb_spx_update_window_size(int pid, int w, int h)
{
    if (pid < 0 || pid > 255 || w < 0 || w > 65535 || h < 0 || h > 65535) {
        return FMRB_SPX_ERR_RANGE;
    }
    return fmrb_app_update_window_size((uint8_t)pid, (uint16_t)w, (uint16_t)h) == FMRB_OK ? 1 : 0;
}

int fmrb_spx_suspend_app(int pid)
{
    return fmrb_app_suspend((int32_t)pid) ? 0 : FMRB_SPX_ERR;
}

int fmrb_spx_resume_app(int pid)
{
    return fmrb_app_resume((int32_t)pid) ? 0 : FMRB_SPX_ERR;
}

int fmrb_spx_reap_app(int pid)
{
    return fmrb_app_reap((int32_t)pid) ? 0 : FMRB_SPX_ERR;
}

int fmrb_spx_spawn_app_req(const char *name, int len)
{
    if (!name || len < 0 || len >= FMRB_MAX_APP_NAME) {
        return FMRB_SPX_ERR_RANGE;
    }
    char namebuf[FMRB_MAX_APP_NAME];
    memcpy(namebuf, name, (size_t)len);
    namebuf[len] = 0;
    int32_t new_pid = -1;
    fmrb_err_t ret = fmrb_app_spawn_app(namebuf, &new_pid);
    return ret == FMRB_OK ? (int)new_pid : FMRB_SPX_ERR;
}

int fmrb_spx_app_info_snapshot(int pid, uint8_t *buf, int cap)
{
    /* Phase 2 will finalize the app-info record layout alongside the desktop
       Spinel port. Not needed for the hello_kernel bring-up. */
    (void)pid; (void)buf; (void)cap;
    return FMRB_SPX_ERR;
}
