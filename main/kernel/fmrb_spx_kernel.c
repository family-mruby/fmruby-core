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
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "fmrb_app.h"
#include "fmrb_kernel.h"
#include "fmrb_msg.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"
#include "fmrb_log.h"
#include "fmrb_transport.h"
#include "fmrb_link_protocol.h"
#include "status_led.h"
#include "boot.h"

static const char *TAG = "spx";

/* fmrb_spx_board_millis / fmrb_spx_log_write and the sp_net_bin_len :binstr
   length publisher live in fmrb_spx_common.c (compiled for any Spinel engine),
   so a mixed mruby-kernel + Spinel-desktop build -- where this file is NOT
   compiled -- still resolves them from the app/gfx shims + common TU. */
extern int sp_net_bin_len;

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

const char *fmrb_spx_windows_snapshot(void)
{
    /* Returned as :binstr (a real Spinel String the Ruby side reads with
       getbyte); ffi_buffer would hand back a :ptr, which has no getbyte. The
       byte length (count * 48) is published in sp_net_bin_len. */
    static uint8_t buf[FMRB_MAX_APPS * FMRB_SPX_WIN_RECORD_SIZE];
    sp_net_bin_len = 0;

    fmrb_window_info_t windows[FMRB_MAX_APPS];
    int32_t count = fmrb_app_get_window_list(windows, FMRB_MAX_APPS);
    if (count < 0) {
        return "";
    }
    if (count > FMRB_MAX_APPS) {
        count = FMRB_MAX_APPS;
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
    sp_net_bin_len = (int)(count * FMRB_SPX_WIN_RECORD_SIZE);
    return (const char *)buf;
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

/* Copy a NUL-terminated C string into a fixed-width, NUL-padded field. */
static void spx_pack_name(uint8_t *dst, int width, const char *src)
{
    memset(dst, 0, (size_t)width);
    if (!src) {
        return;
    }
    size_t n = strnlen(src, (size_t)width);
    memcpy(dst, src, n);
}

const char *fmrb_spx_app_info_snapshot(int pid)
{
    /* :binstr return (see fmrb_spx_windows_snapshot). Empty string when the pid
       has no context, so the Ruby side returns nil. */
    static uint8_t buf[FMRB_SPX_APP_INFO_RECORD_SIZE];
    sp_net_bin_len = 0;

    fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id((int32_t)pid);
    if (!ctx) {
        return "";
    }
    memset(buf, 0, FMRB_SPX_APP_INFO_RECORD_SIZE);
    buf[0] = 1;                                   /* valid */
    buf[1] = ctx->fullscreen ? 1 : 0;             /* fullscreen */
    /* vm_type: mruby=1 lua=2 basic=3 native=4 (matches base_spinel mapping). */
    switch (ctx->vm_type) {
        case FMRB_VM_TYPE_MRUBY: buf[2] = 1; break;
        case FMRB_VM_TYPE_LUA:   buf[2] = 2; break;
        case FMRB_VM_TYPE_BASIC: buf[2] = 3; break;
        default:                 buf[2] = 4; break;  /* native */
    }
    buf[3] = (uint8_t)ctx->load_mode;             /* load_mode */
    spx_pack_name(buf + 4, 32, ctx->app_name);    /* name */
    if (ctx->load_mode == FMRB_LOAD_MODE_FILE && ctx->load_data) {
        spx_pack_name(buf + 36, 128, (const char *)ctx->load_data);  /* path */
    }
    sp_net_bin_len = FMRB_SPX_APP_INFO_RECORD_SIZE;
    return (const char *)buf;
}

const char *fmrb_spx_last_error(void)
{
    /* :binstr return; empty string when there is no error (Ruby returns nil). */
    static uint8_t buf[FMRB_SPX_LAST_ERROR_RECORD_SIZE];
    sp_net_bin_len = 0;

    const char *name = fmrb_app_get_last_error_name();
    const char *msg = fmrb_app_get_last_error_msg();
    if (!name || name[0] == '\0') {
        return "";
    }
    memset(buf, 0, FMRB_SPX_LAST_ERROR_RECORD_SIZE);
    spx_pack_name(buf + 0, 64, name);
    spx_pack_name(buf + 64, 112, msg);
    sp_net_bin_len = FMRB_SPX_LAST_ERROR_RECORD_SIZE;
    return (const char *)buf;
}

int fmrb_spx_set_error_led(int level)
{
    status_led_set_error(level);
    return 0;
}

int fmrb_spx_set_ready(void)
{
    fmrb_kernel_set_ready();
    return 0;
}

int fmrb_spx_check_protocol_version(int timeout_ms)
{
    if (timeout_ms < 0) timeout_ms = 0;
    fmrb_err_t ret = fmrb_transport_check_version((uint32_t)timeout_ms);
    return ret == FMRB_OK ? 1 : 0;
}

int fmrb_spx_check_ga_version(int timeout_ms)
{
    if (timeout_ms < 0) timeout_ms = 0;
    fmrb_err_t ret = fmrb_transport_check_ga_version((uint32_t)timeout_ms);
    return ret == FMRB_OK ? 1 : 0;
}

int fmrb_spx_sync_time_to_host(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    fmrb_control_set_time_t cmd = {
        .tv_sec = (int64_t)tv.tv_sec,
        .tv_usec = (int32_t)tv.tv_usec,
    };
    memset(cmd.tz, 0, sizeof(cmd.tz));
    const char *tz = getenv("TZ");
    if (tz) {
        strncpy(cmd.tz, tz, sizeof(cmd.tz) - 1);
    }

    fmrb_err_t ret = fmrb_transport_send(
        FMRB_LINK_TYPE_CONTROL,
        FMRB_LINK_CONTROL_SET_TIME,
        (const uint8_t *)&cmd,
        sizeof(cmd),
        FMRB_TRANSPORT_TIMEOUT_DEFAULT);
    return ret == FMRB_OK ? 1 : 0;
}
