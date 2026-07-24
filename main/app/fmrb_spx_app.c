/**
 * @file fmrb_spx_app.c
 * @brief Implementation of the Spinel FmrbApp FFI shim (fmrb_spx_app.h).
 *
 * Each function reproduces the body of the matching mruby FmrbApp method
 * (lib/add/picoruby-fmrb-app/ports/esp32/app.c) minus the mruby plumbing,
 * packing structured results into fixed little-endian :binstr records.
 */
#include "fmrb_spx_app.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "fmrb_app.h"
#include "fmrb_gfx.h"
#include "fmrb_err.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"
#include "fmrb_msg_payload.h"
#include "fmrb_file_transfer_msg.h"
#include "fmrb_mem.h"
#include "fmrb_rtos.h"
#include "fmrb_hal.h"
#include "fmrb_hal_time.h"
#include "fmrb_task_config.h"
#include "fmrb_kernel.h"
#include "host_task.h"
#include "usb_task.h"
#include "status_led.h"

#ifdef CONFIG_IDF_TARGET_LINUX
#include <sys/sysinfo.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#else
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "hw_proxy.h"
#endif

#if defined(FMRB_HW_MODERN)
#include "wifi_task.h"
#endif

static const char *TAG = "spxapp";

/* :binstr byte length publisher (defined in fmrb_spx_kernel.c). */
extern int sp_net_bin_len;

/* Little-endian store helpers. */
static inline void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }
static inline void put_u32(uint8_t *p, uint32_t v)
{ p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF); p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF); }

/* Copy a NUL-terminated string into a fixed-width NUL-padded field. */
static void pack_name(uint8_t *dst, int width, const char *src)
{
    memset(dst, 0, (size_t)width);
    if (src) {
        size_t n = strnlen(src, (size_t)width);
        memcpy(dst, src, n);
    }
}

/* ---- instance lifecycle ------------------------------------------------- */

const char *fmrb_spx_app_init(void)
{
    static uint8_t buf[FMRB_SPX_APP_INIT_RECORD_SIZE];
    sp_net_bin_len = 0;

    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return "";
    }

    memset(buf, 0, sizeof(buf));
    pack_name(buf + 0, 32, ctx->app_name);
    buf[32] = ctx->fullscreen ? 1 : 0;
    buf[33] = (!ctx->fullscreen && ctx->rounded_corners) ? 1 : 0;
#ifdef CONFIG_IDF_TARGET_LINUX
    buf[34] = 0;  /* platform: linux */
#else
    buf[34] = 1;  /* platform: esp32 */
#endif
    buf[35] = ctx->headless ? 1 : 0;
    put_u16(buf + 36, ctx->window_width);
    put_u16(buf + 38, ctx->window_height);
    put_u16(buf + 40, ctx->window_pos_x);
    put_u16(buf + 42, ctx->window_pos_y);

    uint8_t has_canvas = 0, has_bg = 0;
    if (!ctx->headless) {
        fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
        if (!gfx_ctx) {
            return "";
        }
        /* Main window canvas: color-key transparency only for non-fullscreen
           windows that kept rounded_corners (transparent color 0x01). */
        fmrb_canvas_handle_t canvas_id = FMRB_CANVAS_SCREEN;
        fmrb_gfx_err_t ret = fmrb_gfx_create_canvas(
            gfx_ctx, ctx->window_width, ctx->window_height, ctx->z_order,
            !ctx->fullscreen && ctx->rounded_corners, 0x01, &canvas_id);
        if (ret != FMRB_GFX_OK) {
            FMRB_LOGE(TAG, "create main canvas failed: %d", ret);
            return "";
        }
        ctx->canvas_id = canvas_id;
        has_canvas = 1;
        put_u16(buf + 46, (uint16_t)canvas_id);
        FMRB_LOGI(TAG, "created canvas %u (%dx%d) for %s",
                  canvas_id, ctx->window_width, ctx->window_height, ctx->app_name);

        /* Background canvas (desktop wallpaper layer, z=0). */
        if (ctx->has_background_canvas) {
            fmrb_canvas_handle_t bg_id = FMRB_CANVAS_SCREEN;
            fmrb_gfx_err_t bg_ret = fmrb_gfx_create_canvas(
                gfx_ctx, ctx->window_width, ctx->window_height, 0, false, 0, &bg_id);
            if (bg_ret == FMRB_GFX_OK) {
                ctx->bg_canvas_id = bg_id;
                has_bg = 1;
                put_u16(buf + 48, (uint16_t)bg_id);
            } else {
                FMRB_LOGE(TAG, "create bg canvas failed: %d", bg_ret);
            }
        }
    }
    buf[44] = has_canvas;
    buf[45] = has_bg;

    sp_net_bin_len = FMRB_SPX_APP_INIT_RECORD_SIZE;
    return (const char *)buf;
}

const char *fmrb_spx_app_recv_message(int timeout_ms, int *type, int *src_pid)
{
    static uint8_t payload[FMRB_MAX_MSG_PAYLOAD_SIZE];
    if (type) *type = -1;
    if (src_pid) *src_pid = -1;
    sp_net_bin_len = 0;

    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return "";
    }
    fmrb_msg_t msg;
    fmrb_err_t ret = fmrb_msg_receive(ctx->app_id, &msg,
                                      (uint32_t)(timeout_ms < 0 ? 0 : timeout_ms));
    if (ret != FMRB_OK) {
        return "";
    }
    uint32_t n = msg.size;
    if (n > sizeof(payload)) n = sizeof(payload);
    memcpy(payload, msg.data, n);
    if (type) *type = (int)msg.type;
    if (src_pid) *src_pid = (int)msg.src_pid;
    sp_net_bin_len = (int)n;
    return (const char *)payload;
}

int fmrb_spx_app_cleanup(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return 0;
    }
    FMRB_LOGI(TAG, "_cleanup: app_id=%d, name=%s", ctx->app_id, ctx->app_name);

    if (ctx->canvas_id != FMRB_CANVAS_SCREEN) {
        fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
        if (gfx_ctx) {
            if (fmrb_gfx_delete_canvas(gfx_ctx, ctx->canvas_id) == FMRB_GFX_OK) {
                ctx->canvas_id = 0;
            }
        }
    }
    fmrb_msg_delete_queue(ctx->app_id);

#ifndef CONFIG_IDF_TARGET_LINUX
    hw_proxy_release_resources((hw_proxy_task_handle_t)fmrb_task_get_current());
#endif
    return 0;
}

int fmrb_spx_app_send_message(int dest_pid, int msg_type, const char *data, int len)
{
    if (!data || len < 0 || len > FMRB_MAX_MSG_PAYLOAD_SIZE) {
        return FMRB_SPX_ERR_RANGE;
    }
    if (dest_pid < 0 || dest_pid > 255) {
        return FMRB_SPX_ERR_RANGE;
    }
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return FMRB_SPX_ERR;
    }
    fmrb_msg_t msg = {
        .type = (fmrb_msg_type_t)msg_type,
        .src_pid = ctx->app_id,
        .size = (uint32_t)len,
    };
    memcpy(msg.data, data, (size_t)len);
    return fmrb_msg_send((fmrb_proc_id_t)dest_pid, &msg, 1000) == FMRB_OK ? 1 : 0;
}

int fmrb_spx_app_set_window_param(int which, int value)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return FMRB_SPX_ERR;
    }
    if (which == 0) {
        ctx->window_pos_x = (uint16_t)value;
    } else if (which == 1) {
        ctx->window_pos_y = (uint16_t)value;
    } else {
        return FMRB_SPX_ERR_RANGE;
    }
    return 0;
}

int fmrb_spx_app_is_file_app(void)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return 0;
    }
    return ctx->load_mode == FMRB_LOAD_MODE_FILE ? 1 : 0;
}

int fmrb_spx_app_create_canvas(int w, int h, int z_offset, int use_transparent, int transparent_color)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return FMRB_SPX_ERR;
    }
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        return FMRB_SPX_ERR_RANGE;
    }
    int slot = -1;
    for (int i = 0; i < FMRB_APP_MAX_EXTRA_CANVAS; i++) {
        if (ctx->extra_canvas_ids[i] == 0) { slot = i; break; }
    }
    if (slot < 0) {
        return FMRB_SPX_ERR;
    }
    fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
    if (!gfx_ctx) {
        return FMRB_SPX_ERR;
    }
    fmrb_canvas_handle_t canvas_id = FMRB_CANVAS_SCREEN;
    fmrb_gfx_err_t ret = fmrb_gfx_create_canvas(
        gfx_ctx, (uint16_t)w, (uint16_t)h, (int16_t)(ctx->z_order + z_offset),
        use_transparent != 0, (uint8_t)transparent_color, &canvas_id);
    if (ret != FMRB_GFX_OK) {
        return FMRB_SPX_ERR;
    }
    ctx->extra_canvas_ids[slot] = canvas_id;
    return (int)canvas_id;
}

int fmrb_spx_app_delete_canvas(int canvas_id)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return FMRB_SPX_ERR;
    }
    for (int i = 0; i < FMRB_APP_MAX_EXTRA_CANVAS; i++) {
        if (ctx->extra_canvas_ids[i] == (uint16_t)canvas_id) {
            fmrb_gfx_context_t gfx_ctx = fmrb_gfx_get_global_context();
            if (gfx_ctx) {
                fmrb_gfx_delete_canvas(gfx_ctx, (fmrb_canvas_handle_t)canvas_id);
            }
            ctx->extra_canvas_ids[i] = 0;
            return 0;
        }
    }
    return FMRB_SPX_ERR_RANGE;  /* not an extra canvas of this app */
}

/* ---- class: process / memory info --------------------------------------- */

const char *fmrb_spx_app_ps(void)
{
    static uint8_t buf[FMRB_MAX_APPS * FMRB_SPX_APP_PS_RECORD_SIZE];
    sp_net_bin_len = 0;

    fmrb_app_info_t list[FMRB_MAX_APPS];
    int32_t count = fmrb_app_ps(list, FMRB_MAX_APPS);
    if (count < 0) count = 0;
    if (count > FMRB_MAX_APPS) count = FMRB_MAX_APPS;

    for (int32_t i = 0; i < count; i++) {
        uint8_t *r = buf + (size_t)i * FMRB_SPX_APP_PS_RECORD_SIZE;
        memset(r, 0, FMRB_SPX_APP_PS_RECORD_SIZE);
        r[0] = (uint8_t)list[i].app_id;
        r[1] = (uint8_t)list[i].state;
        r[2] = (uint8_t)list[i].type;
        r[3] = (uint8_t)list[i].vm_type;
        put_u32(r + 4,  (uint32_t)list[i].gen);
        put_u32(r + 8,  (uint32_t)list[i].stack_high_water);
        put_u32(r + 12, (uint32_t)list[i].mem_total);
        put_u32(r + 16, (uint32_t)list[i].mem_used);
        put_u32(r + 20, (uint32_t)list[i].mem_free);
        put_u32(r + 24, (uint32_t)list[i].mem_frag);
        pack_name(r + 28, 32, list[i].app_name);
    }
    sp_net_bin_len = (int)(count * FMRB_SPX_APP_PS_RECORD_SIZE);
    return (const char *)buf;
}

const char *fmrb_spx_app_heap_info(void)
{
    static uint8_t buf[FMRB_SPX_APP_HEAP_RECORD_SIZE];
    sp_net_bin_len = 0;
    memset(buf, 0, sizeof(buf));

#ifdef CONFIG_IDF_TARGET_LINUX
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        uint32_t total_ram = (uint32_t)(si.totalram * si.mem_unit);
        uint32_t free_ram = (uint32_t)(si.freeram * si.mem_unit);
        put_u32(buf + 0, free_ram);   /* free */
        put_u32(buf + 4, total_ram);  /* total */
        put_u32(buf + 8, free_ram);   /* min_free (no Linux equivalent) */
        put_u32(buf + 12, free_ram);  /* largest_block (no Linux equivalent) */
        /* iram_free / iram_total stay 0 */
    }
#else
    put_u32(buf + 0, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    put_u32(buf + 4, (uint32_t)heap_caps_get_total_size(MALLOC_CAP_DEFAULT));
    put_u32(buf + 8, (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT));
    put_u32(buf + 12, (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    put_u32(buf + 16, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    put_u32(buf + 20, (uint32_t)heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
#endif
    sp_net_bin_len = FMRB_SPX_APP_HEAP_RECORD_SIZE;
    return (const char *)buf;
}

const char *fmrb_spx_app_sys_pool_info(void)
{
    static uint8_t buf[FMRB_SPX_APP_SYSPOOL_RECORD_SIZE];
    sp_net_bin_len = 0;
    memset(buf, 0, sizeof(buf));

    fmrb_pool_stats_t stats;
    if (fmrb_sys_mem_get_stats(&stats) == 0) {
        put_u32(buf + 0, (uint32_t)stats.total_size);
        put_u32(buf + 4, (uint32_t)stats.used_size);
        put_u32(buf + 8, (uint32_t)stats.free_size);
        put_u32(buf + 12, (uint32_t)stats.used_blocks);
        put_u32(buf + 16, (uint32_t)stats.free_blocks);
    }
    sp_net_bin_len = FMRB_SPX_APP_SYSPOOL_RECORD_SIZE;
    return (const char *)buf;
}

const char *fmrb_spx_app_gfx_stats(void)
{
    static uint8_t buf[FMRB_SPX_APP_GFXSTATS_RECORD_SIZE];
    sp_net_bin_len = 0;
    uint32_t cmds = 0, presents = 0;
    fmrb_host_get_gfx_counters(&cmds, &presents);
    put_u32(buf + 0, cmds);
    put_u32(buf + 4, presents);
    sp_net_bin_len = FMRB_SPX_APP_GFXSTATS_RECORD_SIZE;
    return (const char *)buf;
}

const char *fmrb_spx_app_last_error(void)
{
    static uint8_t buf[FMRB_SPX_APP_LASTERR_RECORD_SIZE];
    sp_net_bin_len = 0;

    const char *name = fmrb_app_get_last_error_name();
    const char *msg = fmrb_app_get_last_error_msg();
    if (!name || name[0] == '\0') {
        return "";
    }
    memset(buf, 0, sizeof(buf));
    pack_name(buf + 0, 64, name);
    pack_name(buf + 64, 112, msg);
    sp_net_bin_len = FMRB_SPX_APP_LASTERR_RECORD_SIZE;
    return (const char *)buf;
}

/* ---- class: configuration / clock --------------------------------------- */

#define SPX_CONFIG_MAX_TABLES 16

const char *fmrb_spx_app_config(const char *section, int len)
{
    /* Worst-case packed size: count byte + per table (count byte +
       per kv (1 + key + 2 + val)). Bounded by table/entry/field maxima. */
    static uint8_t buf[1 + SPX_CONFIG_MAX_TABLES *
                       (1 + FMRB_CONFIG_MAX_ENTRIES *
                        (1 + FMRB_CONFIG_KEY_MAX + 2 + FMRB_CONFIG_VAL_MAX))];
    sp_net_bin_len = 0;
    if (!section || len < 0 || len >= 64) {
        return "";
    }
    char namebuf[64];
    memcpy(namebuf, section, (size_t)len);
    namebuf[len] = '\0';

    fmrb_config_table_t *tables =
        (fmrb_config_table_t *)fmrb_sys_malloc(sizeof(fmrb_config_table_t) * SPX_CONFIG_MAX_TABLES);
    if (!tables) {
        return "";
    }
    int table_count = fmrb_kernel_get_config_section(namebuf, tables, SPX_CONFIG_MAX_TABLES);
    if (table_count <= 0) {
        fmrb_sys_free(tables);
        return "";
    }
    if (table_count > SPX_CONFIG_MAX_TABLES) {
        table_count = SPX_CONFIG_MAX_TABLES;
    }

    size_t off = 0;
    buf[off++] = (uint8_t)table_count;
    for (int t = 0; t < table_count; t++) {
        int kv_count = tables[t].count;
        if (kv_count < 0) kv_count = 0;
        if (kv_count > FMRB_CONFIG_MAX_ENTRIES) kv_count = FMRB_CONFIG_MAX_ENTRIES;
        buf[off++] = (uint8_t)kv_count;
        for (int k = 0; k < kv_count; k++) {
            size_t klen = strnlen(tables[t].kv[k].key, FMRB_CONFIG_KEY_MAX);
            size_t vlen = strnlen(tables[t].kv[k].value, FMRB_CONFIG_VAL_MAX);
            buf[off++] = (uint8_t)klen;
            memcpy(buf + off, tables[t].kv[k].key, klen); off += klen;
            put_u16(buf + off, (uint16_t)vlen); off += 2;
            memcpy(buf + off, tables[t].kv[k].value, vlen); off += vlen;
        }
    }
    fmrb_sys_free(tables);
    sp_net_bin_len = (int)off;
    return (const char *)buf;
}

const char *fmrb_spx_app_wallclock(void)
{
    static uint8_t buf[FMRB_SPX_APP_WALLCLOCK_RECORD_SIZE];
    sp_net_bin_len = 0;
    fmrb_wallclock_t wc;
    if (fmrb_hal_time_get_wallclock(&wc) != FMRB_OK) {
        return "";
    }
    put_u16(buf + 0, wc.year);
    put_u16(buf + 2, wc.month);
    put_u16(buf + 4, wc.day);
    put_u16(buf + 6, wc.hour);
    put_u16(buf + 8, wc.minute);
    put_u16(buf + 10, wc.second);
    sp_net_bin_len = FMRB_SPX_APP_WALLCLOCK_RECORD_SIZE;
    return (const char *)buf;
}

const char *fmrb_spx_app_set_wallclock(int year, int month, int day,
                                       int hour, int minute, int second)
{
    static uint8_t buf[FMRB_SPX_APP_WALLCLOCK_RECORD_SIZE];
    sp_net_bin_len = 0;

    struct tm local_tm = {0};
    local_tm.tm_year = year - 1900;
    local_tm.tm_mon = month - 1;
    local_tm.tm_mday = day;
    local_tm.tm_hour = hour;
    local_tm.tm_min = minute;
    local_tm.tm_sec = second;
    local_tm.tm_isdst = -1;

    time_t epoch = mktime(&local_tm);
    if (epoch == (time_t)-1) {
        return "";
    }
    struct timespec ts = { .tv_sec = epoch, .tv_nsec = 0 };
    if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
        return "";
    }
    struct tm utc_tm;
    gmtime_r(&epoch, &utc_tm);
    put_u16(buf + 0, (uint16_t)(utc_tm.tm_year + 1900));
    put_u16(buf + 2, (uint16_t)(utc_tm.tm_mon + 1));
    put_u16(buf + 4, (uint16_t)utc_tm.tm_mday);
    put_u16(buf + 6, (uint16_t)utc_tm.tm_hour);
    put_u16(buf + 8, (uint16_t)utc_tm.tm_min);
    put_u16(buf + 10, (uint16_t)utc_tm.tm_sec);
    sp_net_bin_len = FMRB_SPX_APP_WALLCLOCK_RECORD_SIZE;
    return (const char *)buf;
}

/* ---- class: cursor / power ---------------------------------------------- */

int fmrb_spx_app_enable_cursor(void)
{
    fmrb_host_enable_cursor();
    return 0;
}

int fmrb_spx_app_set_cursor_visible(int visible)
{
    fmrb_host_set_cursor_visible(visible ? true : false);
    return 0;
}

int fmrb_spx_app_reboot(void)
{
    FMRB_LOGI(TAG, "reboot requested");
    fmrb_task_delay_ms(100);
#ifdef CONFIG_IDF_TARGET_LINUX
    exit(0);
#else
    esp_restart();
#endif
    return 0;  /* unreachable */
}

/* ---- class: network / usb ----------------------------------------------- */

const char *fmrb_spx_app_wifi_info(void)
{
    static uint8_t buf[FMRB_SPX_APP_WIFI_RECORD_SIZE];
    sp_net_bin_len = 0;
    memset(buf, 0, sizeof(buf));

#if defined(FMRB_HW_MODERN)
    char ip[16], ssid[33], host[32];
    wifi_get_ip_str(ip, sizeof(ip));
    wifi_get_ssid(ssid, sizeof(ssid));
    wifi_get_hostname(host, sizeof(host));
    buf[0] = wifi_is_connected() ? 1 : 0;
    pack_name(buf + 1, 16, ip);
    pack_name(buf + 17, 33, ssid);
    pack_name(buf + 50, 32, host);
    sp_net_bin_len = FMRB_SPX_APP_WIFI_RECORD_SIZE;
    return (const char *)buf;
#elif defined(CONFIG_IDF_TARGET_LINUX)
    char ip[16] = "127.0.0.1";
    bool connected = false;
    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
                continue;
            }
            struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
            uint32_t a = ntohl(sin->sin_addr.s_addr);
            if ((a >> 24) == 127) {
                continue;
            }
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            connected = true;
            break;
        }
        freeifaddrs(ifaddr);
    }
    buf[0] = connected ? 1 : 0;
    pack_name(buf + 1, 16, ip);
    /* ssid / hostname stay empty on the Linux dev build */
    sp_net_bin_len = FMRB_SPX_APP_WIFI_RECORD_SIZE;
    return (const char *)buf;
#else
    return "";  /* Retro: no networking -> Ruby nil */
#endif
}

const char *fmrb_spx_app_clear_cache(const char *path, int len)
{
    static uint8_t buf[FMRB_SPX_APP_CLEARCACHE_RECORD_SIZE];
    sp_net_bin_len = 0;
    memset(buf, 0, sizeof(buf));
    /* Default: not ok, deleted 0, status -1. */
    put_u32(buf + 1, 0);
    put_u32(buf + 5, (uint32_t)(-1));

    if (!path || len <= 0 || len >= 120) {
        sp_net_bin_len = FMRB_SPX_APP_CLEARCACHE_RECORD_SIZE;
        return (const char *)buf;
    }

    file_cmd_result_t result;
    result.done_sem = fmrb_semaphore_create_binary();
    if (!result.done_sem) {
        sp_net_bin_len = FMRB_SPX_APP_CLEARCACHE_RECORD_SIZE;
        return (const char *)buf;
    }
    result.result = -1;
    memset(&result.data, 0, sizeof(result.data));

    fmrb_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = FMRB_MSG_TYPE_FILE_TRANSFER;
    msg.size = sizeof(file_cmd_t);
    file_cmd_t *cmd = (file_cmd_t *)msg.data;
    cmd->cmd_type = FILE_CMD_RMDIR;
    cmd->result = &result;
    cmd->path_len = (uint16_t)len;
    memcpy(cmd->path, path, (size_t)len);

    if (fmrb_msg_send(PROC_ID_HOST, &msg, 5000) != FMRB_OK) {
        fmrb_semaphore_delete(result.done_sem);
        sp_net_bin_len = FMRB_SPX_APP_CLEARCACHE_RECORD_SIZE;
        return (const char *)buf;
    }
    fmrb_base_type_t wait_ret = fmrb_semaphore_take(result.done_sem, FMRB_MS_TO_TICKS(20000));
    fmrb_semaphore_delete(result.done_sem);

    if (wait_ret == FMRB_PASS) {
        buf[0] = (result.result == 0) ? 1 : 0;
        put_u32(buf + 1, (uint32_t)result.data.rmdir.deleted_count);
        put_u32(buf + 5, (uint32_t)result.data.rmdir.remote_status);
    }
    sp_net_bin_len = FMRB_SPX_APP_CLEARCACHE_RECORD_SIZE;
    return (const char *)buf;
}

const char *fmrb_spx_app_usb_devices(void)
{
    static uint8_t buf[USB_TASK_MAX_DEVICES * FMRB_SPX_APP_USBDEV_RECORD_SIZE];
    sp_net_bin_len = 0;

    fmrb_usb_device_info_t devs[USB_TASK_MAX_DEVICES];
    int count = usb_task_get_device_info(devs, USB_TASK_MAX_DEVICES);
    if (count < 0) count = 0;
    if (count > USB_TASK_MAX_DEVICES) count = USB_TASK_MAX_DEVICES;

    for (int i = 0; i < count; i++) {
        uint8_t *r = buf + (size_t)i * FMRB_SPX_APP_USBDEV_RECORD_SIZE;
        memset(r, 0, FMRB_SPX_APP_USBDEV_RECORD_SIZE);
        r[0] = devs[i].type;               /* fmrb_usb_dev_type_t code */
        r[1] = devs[i].layout_valid ? 1 : 0;
        put_u16(r + 2, devs[i].vid);
        put_u16(r + 4, devs[i].pid);
        r[6] = devs[i].dev_addr;
        r[7] = (uint8_t)devs[i].slot;
        put_u16(r + 8, devs[i].report_byte_len);
    }
    sp_net_bin_len = (int)(count * FMRB_SPX_APP_USBDEV_RECORD_SIZE);
    return (const char *)buf;
}

int fmrb_spx_app_hid_raw_subscribe(int slot)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx) {
        return 0;
    }
    return usb_task_subscribe_raw_reports((int8_t)slot, (uint16_t)ctx->app_id) == FMRB_OK ? 1 : 0;
}

int fmrb_spx_app_hid_raw_unsubscribe(int slot)
{
    return usb_task_unsubscribe_raw_reports((int8_t)slot) == FMRB_OK ? 1 : 0;
}

int fmrb_spx_app_boot_complete(void)
{
    status_led_set_boot_complete();
    return 0;
}
