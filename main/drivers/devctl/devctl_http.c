// Development remote control over HTTP: launch, kill and list apps, and move
// files, so a development loop does not have to drive the launcher through
// synthetic clicks.
//
//   POST /app/launch?path=   GET /app/list        POST /app/kill?pid=
//   GET  /fs/list?path=      GET  /fs/get?path=   PUT  /fs/put?path=
//   DELETE /fs/del?path=     POST /fs/mkdir?path=     GET /fs/md5?path=
//   POST /sys/reboot         POST /sys/gc[?pid=][&max=ms]
//   POST /app/rescan         (the launcher reads /app again)
//   GET  /audio/mute         POST /audio/mute?on=0|1
//   GET  /audio/volume       POST /audio/volume?level=0-10
//   POST /app/checkpoint?pid=[&restart=1][&hold=ms]   (builds with the
//   POST /app/move?pid=&node=[&hold=ms]                 checkpoint gem,
//   GET  /app/checkpoint?pid=                           doc/checkpoint F6)
//
// It lived inside the remote desktop's server (rd_http.c) and moved here so
// Retro can have it too: none of it touches the screen, the encoder or the
// video path, and that file does -- it includes the P4 display and JPEG
// headers, so it cannot be built for the S3 at all. What is here needs only
// the app table, the filesystem and an httpd someone else started.
//
// Modern registers these on the remote desktop's server, which serves the
// viewer as well. Retro starts a small server of its own once WiFi is up
// (devctl_task.c), so a machine with no network pays nothing.
//
// Two kinds of build carry it (doc/remote_guard/plan.md):
// - The development build (FMRB_DEV_REMOTE_CTL, the default) is free: no
//   pairing, and the file endpoints read and write every file, keys included
//   (devctl_fs_unrestricted).
// - A release build arms the guard (main/kernel/fmrb_remote_guard.c): every
//   route here answers 401 unless the owner has turned on "Remote control"
//   in Config (kept across restarts until turned off) and the PC has paired:
//   once, with the number Config shows while "Pair a PC" is open (POST
//   /pair), then the token in an X-Fmrb-Token header or a token= query
//   value. Files are written only under /home, /app and /mnt/sd (403
//   elsewhere) and the keys are never read. /status stays open so the tools
//   can find the board. Turning the switch off closes the WebSockets at once
//   (rg_cut); a request already running finishes, the next is refused.
// Plan and history: doc/dev_remote_ctl/plan.md, doc/remote_guard/plan.md.
//
// Every query value is decoded the same way before a handler sees it
// (ctl_query_value): %XX and "+" (a space), as a query string is written. A
// client may send "/" as it is or as %2F; a path with a space, "%" or a
// non-ASCII name has to be encoded (doc/checkpoint/report/font_pct.md).

#include "devctl_http.h"

#include "fmrb_app.h"
#include "fmrb_log.h"
#include "fmrb_msg.h"
#include "fmrb_hal_file.h"
#include "host_task.h"
#include "fmrb_secret_path.h"
#include "fmrb_remote_guard.h"
#include "fmrb_platform_sys.h"
#include "fmrb_rtos.h"
#include "wifi_task.h"
#if defined(FMRB_HW_MODERN)
#include "fonts/font_ja/font_ja.h"
#endif

#include "esp_http_server.h"
#include "esp_attr.h"
#include "esp_rom_md5.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "devctl";

// ---------------------------------------------------------------
// Development remote control (doc/dev_remote_ctl/plan.md)
//
// Three endpoints so a development loop can start, stop and list apps by name
// instead of driving the launcher through synthetic clicks -- menu, Launcher,
// scroll, Enter -- which is slow and breaks whenever the list moves.
//
// Deliberately no log endpoint. A crash takes WiFi down with it, so the log
// that matters would be the one that never arrives; boot and crash logs come
// from a serial capture held open for the session instead.
//
// A control plane for development. The development build leaves it open to
// the LAN, like the rest of the remote desktop there; a release build puts
// every route behind the guard (devctl_guard_admit, below).
//
// Called straight from the httpd task, the way debugd calls the same functions
// from its own. That task has 8KB of stack against debugd's 6KB, so spawning
// from here is no tighter than spawning from there.
// ---------------------------------------------------------------

// The query as it comes (a name encoded as %XX takes three times its bytes;
// the httpd's URI limit is 512) and a path once decoded.
#define RD_CTL_QUERY_MAX 512
#define RD_CTL_PATH_MAX  128

static esp_err_t ctl_json(httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static int ctl_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Undo the query string's encoding in place: %XX and "+" for a space.
// httpd_query_key_value hands the value back still encoded. A "%" not
// followed by two hex digits is kept as it is. False for %00, which would cut
// the value short where the client did not mean it to end.
static bool ctl_url_decode(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        int hi, lo;
        if (*r == '%' && (hi = ctl_hex(r[1])) >= 0 && (lo = ctl_hex(r[2])) >= 0) {
            char c = (char)((hi << 4) | lo);
            if (c == '\0') return false;
            *w++ = c;
            r += 2;
        } else if (*r == '+') {
            *w++ = ' ';
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
    return true;
}

// One key out of the request's query string, decoded (ctl_url_decode), so
// every route takes the same spelling of a path or a name. False when the key
// is missing or does not fit out decoded. Bad input answers with JSON rather
// than dropping the connection: a development tool should be told what it got
// wrong.
static bool ctl_query_value(httpd_req_t *req, const char *key, char *out, size_t out_len)
{
    char q[RD_CTL_QUERY_MAX];
    char v[RD_CTL_QUERY_MAX];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
        return false;
    }
    if (httpd_query_key_value(q, key, v, sizeof(v)) != ESP_OK) {
        return false;
    }
    if (!ctl_url_decode(v)) {
        return false;
    }
    size_t n = strlen(v);
    if (n >= out_len) {
        return false;
    }
    memcpy(out, v, n + 1);
    return true;
}

// ---------------------------------------------------------------
// The release build's guard (doc/remote_guard G2)
//
// The rules live in main/kernel/fmrb_remote_guard.c, shared with FmrbSystem
// (Config turns it on and shows the number). Here: where the switch and the
// tokens' hashes are kept (NVS, up to four PCs), the randomness for numbers
// and tokens, SHA-256, a lock between the httpd task and the app that runs
// Config, closing the connections when the switch goes off, and the 401 the
// tools read.
// ---------------------------------------------------------------

#define RG_NVS_NAMESPACE "fmrb_rguard"
#define RG_NVS_KEY       "state"
// The first version's table (plaintext tokens, 2026-11-16, never released):
// erased when the guard is armed.
#define RG_NVS_KEY_OLD   "tokens"
#define RG_TOKEN_HEADER  "X-Fmrb-Token"

// Created when the guard is armed (a release build only). In external RAM
// like the guard's own state: a development build pays nothing internal.
EXT_RAM_BSS_ATTR static SemaphoreHandle_t s_rg_mutex;
// The server the guard's connections are on, for rg_cut.
EXT_RAM_BSS_ATTR static httpd_handle_t s_rg_server;

static bool rg_load(void *blob, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(RG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    if (nvs_erase_key(h, RG_NVS_KEY_OLD) == ESP_OK) {
        nvs_commit(h);
        FMRB_LOGW(TAG, "remote control: dropped the old token table; pair the PCs again");
    }
    size_t n = len;
    esp_err_t err = nvs_get_blob(h, RG_NVS_KEY, blob, &n);
    nvs_close(h);
    return err == ESP_OK && n == len;
}

static bool rg_save(const void *blob, size_t len)
{
    // The table lives in external RAM; hand NVS a copy on this stack.
    uint8_t copy[FMRB_RGUARD_BLOB_SIZE];
    if (len > sizeof(copy)) return false;
    memcpy(copy, blob, len);
    nvs_handle_t h;
    if (nvs_open(RG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, RG_NVS_KEY, copy, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) FMRB_LOGW(TAG, "could not save the remote control state: %d", (int)err);
    return err == ESP_OK;
}

static void rg_random(void *buf, size_t len)
{
    esp_fill_random(buf, len);  // WiFi is up, so the RF noise feeds it
}

static void rg_sha256(const void *in, size_t len, uint8_t out[FMRB_RGUARD_HASH_BYTES])
{
    // Already linked for WiFi; the one-shot form keeps its context on the stack.
    if (mbedtls_sha256((const unsigned char *)in, len, out, 0) != 0) {
        // Never matches a stored hash: a failure refuses rather than admits.
        memset(out, 0xff, FMRB_RGUARD_HASH_BYTES);
        out[0] ^= 0x5a;
    }
}

static void rg_lock(void)
{
    if (s_rg_mutex) xSemaphoreTake(s_rg_mutex, portMAX_DELAY);
}

static void rg_unlock(void)
{
    if (s_rg_mutex) xSemaphoreGive(s_rg_mutex);
}

#ifdef CONFIG_HTTPD_WS_SUPPORT
// On the httpd task: close every WebSocket (the viewer's input, the H.264
// video). The handshake was the only check, so these would carry on after
// the switch went off. Plain requests are checked one by one and need no
// closing; the MJPEG stream watches fmrb_rguard_epoch itself (its session is
// detached, and closing it under the stream task is not safe).
static void rg_cut_work(void *arg)
{
    (void)arg;
    int fds[CONFIG_LWIP_MAX_SOCKETS];
    size_t n = sizeof(fds) / sizeof(fds[0]);
    if (httpd_get_client_list(s_rg_server, &n, fds) != ESP_OK) return;
    int closed = 0;
    for (size_t i = 0; i < n; i++) {
        if (httpd_ws_get_fd_info(s_rg_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_sess_trigger_close(s_rg_server, fds[i]);
            closed++;
        }
    }
    if (closed) FMRB_LOGI(TAG, "remote control: closed %d connection(s)", closed);
}
#endif

static void rg_cut(void)
{
#ifdef CONFIG_HTTPD_WS_SUPPORT
    if (s_rg_server) httpd_queue_work(s_rg_server, rg_cut_work, NULL);
#endif
}

static const fmrb_rguard_ops_t s_rg_ops = {
    .load = rg_load, .save = rg_save, .random = rg_random, .sha256 = rg_sha256,
    .lock = rg_lock, .unlock = rg_unlock, .cut = rg_cut,
};

static void devctl_guard_arm(httpd_handle_t server)
{
    if (fmrb_rguard_state() != FMRB_RGUARD_NONE) return;
    s_rg_server = server;
    if (!s_rg_mutex) s_rg_mutex = xSemaphoreCreateMutex();
    fmrb_rguard_arm(&s_rg_ops);
}

fmrb_rguard_verdict_t devctl_guard_verdict(httpd_req_t *req)
{
    if (fmrb_rguard_state() == FMRB_RGUARD_NONE) return FMRB_RGUARD_ALLOW;
    char tok[FMRB_RGUARD_TOKEN_HEX_LEN + 8];
    tok[0] = '\0';
    if (httpd_req_get_hdr_value_str(req, RG_TOKEN_HEADER, tok, sizeof(tok)) != ESP_OK) {
        // A browser cannot add a header to an <img> or a WebSocket.
        char q[RD_CTL_QUERY_MAX];
        tok[0] = '\0';
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK ||
            httpd_query_key_value(q, "token", tok, sizeof(tok)) != ESP_OK) {
            tok[0] = '\0';
        }
    }
    return fmrb_rguard_check(tok);
}

bool devctl_guard_admit(httpd_req_t *req)
{
    fmrb_rguard_verdict_t v = devctl_guard_verdict(req);
    if (v == FMRB_RGUARD_ALLOW) return true;
    char body[320];
    snprintf(body, sizeof(body), "{\"ok\":false,\"guard\":\"%s\",\"err\":\"%s\"}",
             fmrb_rguard_verdict_name(v), fmrb_rguard_reason(v));
    ctl_json(req, "401 Unauthorized", body);
    return false;
}

// Every guarded route goes through here: user_ctx is the route as it was
// written (a static httpd_uri_t), so the handlers stay as they are.
static esp_err_t guarded_handler(httpd_req_t *req)
{
    if (!devctl_guard_admit(req)) return ESP_OK;
    const httpd_uri_t *uri = (const httpd_uri_t *)req->user_ctx;
    return uri->handler(req);
}

// POST /pair?code=NNNNNN -> {"ok":true,"token":"<hex>"}. Open to anyone (it is
// how a PC gets in), so it gives nothing away: it answers only while "Pair a
// PC" is open in Config, a wrong number counts, and after
// FMRB_RGUARD_CODE_TRIES of them pairing stops until "Pair a PC" is opened
// again. A development build answers "free".
static esp_err_t pair_handler(httpd_req_t *req)
{
    if (fmrb_rguard_state() == FMRB_RGUARD_NONE) {
        return ctl_json(req, "200 OK", "{\"ok\":true,\"guard\":\"free\",\"token\":\"\"}");
    }
    char code[16];
    if (!ctl_query_value(req, "code", code, sizeof(code))) code[0] = '\0';
    char tok[FMRB_RGUARD_TOKEN_HEX_LEN + 1];
    int left = 0;
    fmrb_rguard_pair_t r = fmrb_rguard_pair(code, tok, &left);
    char body[256];
    switch (r) {
    case FMRB_RGUARD_PAIR_OK:
        FMRB_LOGI(TAG, "remote control: a PC paired (%d paired)", fmrb_rguard_paired());
        snprintf(body, sizeof(body), "{\"ok\":true,\"guard\":\"open\",\"token\":\"%s\"}", tok);
        return ctl_json(req, "200 OK", body);
    case FMRB_RGUARD_PAIR_CLOSED:
        return ctl_json(req, "403 Forbidden",
                        "{\"ok\":false,\"guard\":\"closed\",\"err\":\"remote control is off on this "
                        "board: turn on Config > Remote control, open Pair a PC there, then send the "
                        "number it shows\"}");
    case FMRB_RGUARD_PAIR_NOT_PAIRING:
        return ctl_json(req, "403 Forbidden",
                        "{\"ok\":false,\"guard\":\"not_pairing\",\"err\":\"the board is not taking new "
                        "PCs now: open Config > Remote control > Pair a PC on the board, then send the "
                        "number it shows while it is open\"}");
    case FMRB_RGUARD_PAIR_WRONG:
        FMRB_LOGW(TAG, "remote control: a wrong pairing number (%d tries left)", left);
        snprintf(body, sizeof(body),
                 "{\"ok\":false,\"guard\":\"wrong\",\"left\":%d,\"err\":\"that is not the number "
                 "shown in Config > Remote control > Pair a PC (%d tries left)\"}", left, left);
        return ctl_json(req, "403 Forbidden", body);
    case FMRB_RGUARD_PAIR_LOCKED:
    default:
        FMRB_LOGW(TAG, "remote control: pairing locked after wrong numbers");
        return ctl_json(req, "403 Forbidden",
                        "{\"ok\":false,\"guard\":\"locked\",\"err\":\"too many wrong numbers: leave "
                        "Pair a PC in Config and open it again for a new number\"}");
    }
}

// GET /auth -> 200 {"ok":true} when this request would be let in, the 401
// otherwise. For a tool or the viewer to check its token before a WebSocket,
// whose refusal cannot carry a reason (the handshake is answered first).
static esp_err_t auth_handler(httpd_req_t *req)
{
    if (!devctl_guard_admit(req)) return ESP_OK;
    const char *g = fmrb_rguard_state() == FMRB_RGUARD_NONE ? "free" : "open";
    char body[64];
    snprintf(body, sizeof(body), "{\"ok\":true,\"guard\":\"%s\"}", g);
    return ctl_json(req, "200 OK", body);
}

static esp_err_t launch_handler(httpd_req_t *req)
{
    char path[RD_CTL_PATH_MAX];
    if (!ctl_query_value(req, "path", path, sizeof(path))) {
        return ctl_json(req, "400 Bad Request",
                        "{\"ok\":false,\"err\":\"path required\"}");
    }
    if (path[0] == '\0') {
        return ctl_json(req, "400 Bad Request",
                        "{\"ok\":false,\"err\":\"path empty\"}");
    }

    int32_t pid = -1;
    fmrb_err_t err = fmrb_app_spawn_app(path, &pid);
    if (err != FMRB_OK) {
        char body[96];
        snprintf(body, sizeof(body), "{\"ok\":false,\"err\":%d}", (int)err);
        FMRB_LOGW(TAG, "dev ctl: launch %s failed (%d)", path, (int)err);
        return ctl_json(req, "500 Internal Server Error", body);
    }

    char body[64];
    snprintf(body, sizeof(body), "{\"ok\":true,\"pid\":%d}", (int)pid);
    FMRB_LOGI(TAG, "dev ctl: launched %s as pid %d", path, (int)pid);
    return ctl_json(req, "200 OK", body);
}

static esp_err_t kill_handler(httpd_req_t *req)
{
    char pidstr[16];
    if (!ctl_query_value(req, "pid", pidstr, sizeof(pidstr))) {
        return ctl_json(req, "400 Bad Request",
                        "{\"ok\":false,\"err\":\"pid required\"}");
    }
    int pid = atoi(pidstr);

    // User app slots only. The kernel, the host and the system app are not
    // ours to stop from a development endpoint, and killing has a known way of
    // hanging depending on which task asks (doc/archive/app_kill_fix) -- keeping to the
    // slots a development loop spawns into is the narrow, safe case.
    if (pid < PROC_ID_USER_APP0 || pid >= PROC_ID_USER_APP_END) {
        return ctl_json(req, "400 Bad Request",
                        "{\"ok\":false,\"err\":\"user app pids only\"}");
    }

    bool ok = fmrb_app_kill(pid);
    FMRB_LOGI(TAG, "dev ctl: kill pid %d -> %s", pid, ok ? "ok" : "failed");
    return ctl_json(req, ok ? "200 OK" : "404 Not Found",
                    ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"no such app\"}");
}

static const char *ctl_state_name(fmrb_proc_state_t s)
{
    switch (s) {
    case PROC_STATE_FREE:      return "FREE";
    case PROC_STATE_INIT:      return "INIT";
    case PROC_STATE_RUNNING:   return "RUNNING";
    case PROC_STATE_SUSPENDED: return "SUSPENDED";
    case PROC_STATE_STOPPING:  return "STOPPING";
    default:                   return "UNKNOWN";
    }
}

static esp_err_t list_handler(httpd_req_t *req)
{
    fmrb_app_info_t list[FMRB_MAX_APPS];
    int32_t n = fmrb_app_ps(list, FMRB_MAX_APPS);
    if (n < 0) {
        n = 0;
    }

    // Room for every slot; the write is bounded anyway so a longer name than
    // expected truncates the document rather than the stack.
    char body[768];
    size_t off = 0;
    off += (size_t)snprintf(body + off, sizeof(body) - off, "{\"apps\":[");
    for (int32_t i = 0; i < n && off < sizeof(body); i++) {
        off += (size_t)snprintf(body + off, sizeof(body) - off,
                                "%s{\"pid\":%d,\"name\":\"%s\",\"state\":\"%s\"}",
                                i ? "," : "", (int)list[i].app_id,
                                list[i].app_name, ctl_state_name(list[i].state));
    }
    if (off < sizeof(body)) {
        off += (size_t)snprintf(body + off, sizeof(body) - off, "]}");
    }
    body[sizeof(body) - 1] = '\0';
    return ctl_json(req, "200 OK", body);
}

// ---------------------------------------------------------------
// Development file access: /fs/list, /fs/get, /fs/put, /fs/del, /fs/mkdir
//
// Moving files to and from the device used to go over BLE, which is fine for
// a source file and hopeless for a JPEG or an MJPEG. WiFi already carries the
// MJPEG stream at several hundred KB/s, so the same server now moves files:
// pull an exported slide off the card, push an app into /app and start it
// with /app/launch without a reflash.
//
// Paths go through the HAL resolver, the same as File does, and a ".."
// segment is refused before resolving. What else is refused depends on the
// build, decided in one place (devctl_fs_unrestricted): the development build
// reaches everything under "/" -- /etc and /usr included, and the files that
// hold keys and passwords -- so /home can be backed up and restored over WiFi
// and a firmware's storage brought up to date without a full flash
// (doc/remote_guard G0). Otherwise only the four roots apps use (/app, /home,
// /usr/share, /mnt/sd) are reachable, files are written only under /home,
// /app and /mnt/sd, and the keys are never read: a release build (G1), whose
// routes are also behind the guard (G2).
//
// Transfers stream through one static 4KB buffer rather than the httpd task's
// 8KB stack. A put writes to "<path>.part" and renames at the end so a dropped
// connection leaves no half-written file under the real name.
// ---------------------------------------------------------------

#define RD_FS_VPATH_MAX 192
#define RD_FS_RPATH_MAX 256
#define RD_FS_IO_CHUNK  4096

// The buffer only meets memcpy on both sides: httpd_req_recv / send copy
// through lwIP, and fwrite hands it to LittleFS, which copies it into its own
// internal cache before touching flash (the SD path bounces it into a DMA
// buffer inside sdmmc_write_sectors). So it can live in PSRAM. The S3 keeps it
// internal until that is verified on a Retro board
// (doc/iram_reduction/report/r2.md).
//
// On the S3 it is taken from the heap when the server starts (devctl_http_
// register), not kept in .bss: the server exists only once WiFi is up, and a
// release build carries this file too, so a board that never starts WiFi
// should not pay 4KB of internal RAM for it.
#if CONFIG_IDF_TARGET_ESP32S3
static uint8_t *s_fs_buf;
#define RD_FS_BUF_SIZE RD_FS_IO_CHUNK
#else
EXT_RAM_BSS_ATTR static uint8_t s_fs_buf[RD_FS_IO_CHUNK];
#define RD_FS_BUF_SIZE sizeof(s_fs_buf)
#endif

bool devctl_fs_unrestricted(void)
{
    // The development build; a release build is built with
    // FMRB_DEV_REMOTE_CTL=OFF (rake build:esp32 with FMRB_RELEASE=1 does so).
#ifdef FMRB_DEV_REMOTE_CTL
    return true;
#else
    return false;
#endif
}

static const char *const s_fs_roots[] = { "/mnt/sd", "/home", "/app", "/usr/share" };

// Reject a path containing a ".." segment, and -- unless the build is
// unrestricted -- anything outside the four roots.
static bool fs_path_allowed(const char *vpath)
{
    if (vpath[0] != '/') return false;
    for (const char *p = vpath; *p; p++) {
        if (p[0] == '.' && p[1] == '.' &&
            (p[2] == '\0' || p[2] == '/') && (p == vpath || p[-1] == '/')) {
            return false;
        }
    }
    if (devctl_fs_unrestricted()) return true;
    for (size_t i = 0; i < sizeof(s_fs_roots) / sizeof(s_fs_roots[0]); i++) {
        size_t n = strlen(s_fs_roots[i]);
        if (strncmp(vpath, s_fs_roots[i], n) == 0 &&
            (vpath[n] == '\0' || vpath[n] == '/')) {
            return true;
        }
    }
    return false;
}

// Query "path" -> checked virtual path and resolved real path. Answers the
// request itself on failure so the handlers can just return. write: the
// request changes the file system (put, del, mkdir), which a restricted build
// allows only under /home and /app (doc/remote_guard G1).
static bool fs_req_path(httpd_req_t *req, char *vpath, size_t vlen,
                        char *rpath, size_t rlen, bool write)
{
    if (!ctl_query_value(req, "path", vpath, vlen) || vpath[0] == '\0') {
        ctl_json(req, "400 Bad Request", "{\"ok\":false,\"err\":\"path required\"}");
        return false;
    }
    if (!fs_path_allowed(vpath)) {
        ctl_json(req, "400 Bad Request",
                 "{\"ok\":false,\"err\":\"path outside /mnt/sd, /home, /app, /usr/share\"}");
        return false;
    }
    if (write && !devctl_fs_unrestricted() && !fmrb_rguard_write_allowed(vpath)) {
        ctl_json(req, "403 Forbidden",
                 "{\"ok\":false,\"err\":\"a release build changes files only under /home, /app and /mnt/sd "
                 "(the rest is the firmware's own; update it by flashing)\"}");
        return false;
    }
    fmrb_hal_file_resolve_path(vpath, rpath, rlen);
    return true;
}

// Append one JSON string value with the two characters that would break it
// escaped. Returns the new offset; stops quietly at the end of the buffer.
static size_t fs_json_str(char *out, size_t cap, size_t off, const char *s)
{
    if (off < cap) out[off++] = '"';
    for (; *s && off + 2 < cap; s++) {
        if (*s == '"' || *s == '\\') out[off++] = '\\';
        out[off++] = *s;
    }
    if (off < cap) out[off++] = '"';
    return off;
}

static esp_err_t fs_list_handler(httpd_req_t *req)
{
    char vpath[RD_FS_VPATH_MAX];
    char rpath[RD_FS_RPATH_MAX];
    if (!fs_req_path(req, vpath, sizeof(vpath), rpath, sizeof(rpath), false)) return ESP_OK;

    DIR *dir = opendir(rpath);
    if (!dir) {
        return ctl_json(req, "404 Not Found", "{\"ok\":false,\"err\":\"no such directory\"}");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, "{\"ok\":true,\"entries\":[", HTTPD_RESP_USE_STRLEN);
    char line[320];
    char child[RD_FS_RPATH_MAX + 260];  // parent + "/" + a 255-byte name
    struct stat st;
    struct dirent *ent;
    bool first = true;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        snprintf(child, sizeof(child), "%s/%s", rpath, ent->d_name);
        long size = 0;
        bool is_dir = (ent->d_type == DT_DIR);
        if (stat(child, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            size = (long)st.st_size;
        }
        size_t off = (size_t)snprintf(line, sizeof(line), "%s{\"name\":", first ? "" : ",");
        off = fs_json_str(line, sizeof(line), off, ent->d_name);
        off += (size_t)snprintf(line + off, sizeof(line) - off, ",\"size\":%ld,\"dir\":%s}",
                                size, is_dir ? "true" : "false");
        httpd_resp_send_chunk(req, line, off);
        first = false;
    }
    closedir(dir);
    httpd_resp_send_chunk(req, "]}", 2);
    return httpd_resp_send_chunk(req, NULL, 0);
}

// Files that hold secrets: the WiFi settings (/home/wifi.toml, /etc/wifi.toml)
// hold the network's password, secrets.toml (/home, /etc) the API keys (the
// tts key) and /home/asterism the board's Asterism credentials (the .tar with
// the private key). The development build hands them out like any other file
// -- it is how /home is backed up with its keys (doc/remote_guard G0) -- and
// the tools that fetch them keep them out of their output. A restricted build
// never reads them: anyone on the LAN can call this server, and it has no
// settings window like the BLE console's (fmrb_secret_path.h, which this
// shares, doc/ruby_asterism/report/cf2.md). Secrets can be put, listed and
// deleted either way.
static esp_err_t fs_get_handler(httpd_req_t *req)
{
    char vpath[RD_FS_VPATH_MAX];
    char rpath[RD_FS_RPATH_MAX];
    if (!fs_req_path(req, vpath, sizeof(vpath), rpath, sizeof(rpath), false)) return ESP_OK;
    fmrb_path_class_t cls = fmrb_path_classify(vpath);
    if (!devctl_fs_unrestricted() &&
        (cls == FMRB_PATH_SECRET || cls == FMRB_PATH_PASSWORD)) {
        return ctl_json(req, "403 Forbidden",
                        "{\"ok\":false,\"err\":\"this file holds keys or passwords and is not read remotely\"}");
    }

    FILE *f = fopen(rpath, "rb");
    if (!f) {
        return ctl_json(req, "404 Not Found", "{\"ok\":false,\"err\":\"no such file\"}");
    }
    httpd_resp_set_type(req, "application/octet-stream");
    size_t total = 0;
    size_t n;
    while ((n = fread(s_fs_buf, 1, RD_FS_BUF_SIZE, f)) > 0) {
        if (httpd_resp_send_chunk(req, (const char *)s_fs_buf, n) != ESP_OK) {
            fclose(f);
            FMRB_LOGW(TAG, "dev fs: get %s aborted after %u bytes", vpath, (unsigned)total);
            return ESP_FAIL;
        }
        total += n;
    }
    fclose(f);
    FMRB_LOGI(TAG, "dev fs: get %s (%u bytes)", vpath, (unsigned)total);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t fs_put_handler(httpd_req_t *req)
{
    char vpath[RD_FS_VPATH_MAX];
    char rpath[RD_FS_RPATH_MAX];
    if (!fs_req_path(req, vpath, sizeof(vpath), rpath, sizeof(rpath), true)) return ESP_OK;

    char tmp[RD_FS_RPATH_MAX + 8];
    snprintf(tmp, sizeof(tmp), "%s.part", rpath);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return ctl_json(req, "500 Internal Server Error",
                        "{\"ok\":false,\"err\":\"cannot create file (directory missing?)\"}");
    }
    size_t remaining = req->content_len;
    size_t total = 0;
    while (remaining > 0) {
        size_t want = remaining < RD_FS_BUF_SIZE ? remaining : RD_FS_BUF_SIZE;
        int got = httpd_req_recv(req, (char *)s_fs_buf, want);
        if (got <= 0) {
            if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
            fclose(f);
            unlink(tmp);
            FMRB_LOGW(TAG, "dev fs: put %s aborted after %u bytes", vpath, (unsigned)total);
            return ESP_FAIL;
        }
        if (fwrite(s_fs_buf, 1, (size_t)got, f) != (size_t)got) {
            fclose(f);
            unlink(tmp);
            return ctl_json(req, "500 Internal Server Error",
                            "{\"ok\":false,\"err\":\"write failed (disk full?)\"}");
        }
        remaining -= (size_t)got;
        total += (size_t)got;
    }
    fclose(f);
    // A file someone keeps open (the kanji dictionary) is let go of first:
    // LittleFS removes no open file. It is opened again after the rename.
    const bool watched = fmrb_hal_file_change(rpath, FMRB_FILE_CHANGE_BEGIN);
    const bool old_gone = unlink(rpath) == 0;  // rename does not replace on every filesystem here
    if (rename(tmp, rpath) != 0) {
        unlink(tmp);
        if (watched) {
            fmrb_hal_file_change(rpath, old_gone ? FMRB_FILE_CHANGE_REMOVED : FMRB_FILE_CHANGE_WRITTEN);
        }
        return ctl_json(req, "500 Internal Server Error",
                        "{\"ok\":false,\"err\":\"rename failed\"}");
    }
    if (watched) fmrb_hal_file_change(rpath, FMRB_FILE_CHANGE_WRITTEN);
    char body[64];
    snprintf(body, sizeof(body), "{\"ok\":true,\"size\":%u}", (unsigned)total);
    FMRB_LOGI(TAG, "dev fs: put %s (%u bytes)", vpath, (unsigned)total);
    return ctl_json(req, "200 OK", body);
}

static esp_err_t fs_del_handler(httpd_req_t *req)
{
    char vpath[RD_FS_VPATH_MAX];
    char rpath[RD_FS_RPATH_MAX];
    if (!fs_req_path(req, vpath, sizeof(vpath), rpath, sizeof(rpath), true)) return ESP_OK;

    struct stat st;
    if (stat(rpath, &st) != 0) {
        return ctl_json(req, "404 Not Found", "{\"ok\":false,\"err\":\"no such file\"}");
    }
    // As in put: whoever keeps the file open closes it first.
    const bool watched = !S_ISDIR(st.st_mode) && fmrb_hal_file_change(rpath, FMRB_FILE_CHANGE_BEGIN);
    int rc = S_ISDIR(st.st_mode) ? rmdir(rpath) : unlink(rpath);
    if (watched) {
        fmrb_hal_file_change(rpath, rc == 0 ? FMRB_FILE_CHANGE_REMOVED : FMRB_FILE_CHANGE_WRITTEN);
    }
    if (rc != 0) {
        return ctl_json(req, "500 Internal Server Error",
                        "{\"ok\":false,\"err\":\"remove failed (directory not empty?)\"}");
    }
    FMRB_LOGI(TAG, "dev fs: del %s", vpath);
    return ctl_json(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t fs_mkdir_handler(httpd_req_t *req)
{
    char vpath[RD_FS_VPATH_MAX];
    char rpath[RD_FS_RPATH_MAX];
    if (!fs_req_path(req, vpath, sizeof(vpath), rpath, sizeof(rpath), true)) return ESP_OK;

    struct stat st;
    if (stat(rpath, &st) == 0 && S_ISDIR(st.st_mode)) {
        return ctl_json(req, "200 OK", "{\"ok\":true,\"existed\":true}");
    }
    if (mkdir(rpath, 0777) != 0) {
        return ctl_json(req, "500 Internal Server Error",
                        "{\"ok\":false,\"err\":\"mkdir failed (parent missing?)\"}");
    }
    FMRB_LOGI(TAG, "dev fs: mkdir %s", vpath);
    return ctl_json(req, "200 OK", "{\"ok\":true}");
}

// A file's MD5, so a tool can tell whether the copy on the board matches its
// own without fetching it (rake flash:app_sync compares the build's storage
// tree with the board's). Read through the same 4KB buffer as get; the ROM's
// MD5 needs no table of its own. Change detection, not security. Same rules
// as get: a restricted build does not hash a key either.
static esp_err_t fs_md5_handler(httpd_req_t *req)
{
    char vpath[RD_FS_VPATH_MAX];
    char rpath[RD_FS_RPATH_MAX];
    if (!fs_req_path(req, vpath, sizeof(vpath), rpath, sizeof(rpath), false)) return ESP_OK;
    fmrb_path_class_t cls = fmrb_path_classify(vpath);
    if (!devctl_fs_unrestricted() &&
        (cls == FMRB_PATH_SECRET || cls == FMRB_PATH_PASSWORD)) {
        return ctl_json(req, "403 Forbidden",
                        "{\"ok\":false,\"err\":\"this file holds keys or passwords and is not read remotely\"}");
    }
    FILE *f = fopen(rpath, "rb");
    if (!f) {
        return ctl_json(req, "404 Not Found", "{\"ok\":false,\"err\":\"no such file\"}");
    }
    md5_context_t ctx;
    esp_rom_md5_init(&ctx);
    size_t total = 0;
    size_t n;
    while ((n = fread(s_fs_buf, 1, RD_FS_BUF_SIZE, f)) > 0) {
        esp_rom_md5_update(&ctx, s_fs_buf, (uint32_t)n);
        total += n;
    }
    fclose(f);
    uint8_t digest[ESP_ROM_MD5_DIGEST_LEN];
    esp_rom_md5_final(digest, &ctx);
    char body[96];
    int off = snprintf(body, sizeof(body), "{\"ok\":true,\"size\":%u,\"md5\":\"", (unsigned)total);
    for (int i = 0; i < ESP_ROM_MD5_DIGEST_LEN; i++) {
        off += snprintf(body + off, sizeof(body) - (size_t)off, "%02x", digest[i]);
    }
    snprintf(body + off, sizeof(body) - (size_t)off, "\"}");
    return ctl_json(req, "200 OK", body);
}

// How full the filesystem under path is (the storage partition, or the SD
// card for a path under /mnt/sd): {"ok":true,"total":N,"free":N} in bytes.
// The tools that put the test apps on a board look before they send
// (tools/fmrb_rd_fs.rb tests push, doc/flash_layout F1). Only sizes, so a
// restricted build answers it too.
static esp_err_t fs_df_handler(httpd_req_t *req)
{
    char vpath[RD_FS_VPATH_MAX];
    char rpath[RD_FS_RPATH_MAX];
    if (!fs_req_path(req, vpath, sizeof(vpath), rpath, sizeof(rpath), false)) return ESP_OK;
    uint64_t total = 0, free_bytes = 0;
    if (fmrb_hal_file_statfs(vpath, &total, &free_bytes) != FMRB_OK) {
        return ctl_json(req, "500 Internal Server Error",
                        "{\"ok\":false,\"err\":\"cannot read the filesystem's size\"}");
    }
    char body[80];
    snprintf(body, sizeof(body), "{\"ok\":true,\"total\":%llu,\"free\":%llu}",
             (unsigned long long)total, (unsigned long long)free_bytes);
    return ctl_json(req, "200 OK", body);
}

// Restart the board, as FmrbSystem.reboot does: after /home is written back
// (rake flash:keep_home) the WiFi settings and the keys are read again only at
// boot. The answer goes out first; the reset follows a moment later.
static esp_err_t sys_reboot_handler(httpd_req_t *req)
{
    ctl_json(req, "200 OK", "{\"ok\":true}");
    FMRB_LOGW(TAG, "reboot requested over HTTP");
    fmrb_host_audio_flush_save();  // a mute / volume change still waiting to be saved
    fmrb_task_delay_ms(300);
    fmrb_platform_sys_power(FMRB_SYS_POWER_RESET);
    return ESP_OK;
}

// A full collection in every app (or in pid alone), the same as the debug
// server's gc (fmrb_app_gc_all): each app collects in its own task at its next
// message wait, a Spinel app sweeping its strings too, and the answer comes once
// all have or max ms (default 3000) have passed. For the board's soak
// (rake regress:soak BOARD=), so a reading after it holds only what is live,
// as on the sim. {"ok":true,"apps":[{"pid","name","status"}]}, status "done",
// "timeout", "skipped" (an engine that does not answer) or "gone".
static esp_err_t sys_gc_handler(httpd_req_t *req)
{
    char val[16];
    int32_t pid = -1;
    int max_ms = 0;
    if (ctl_query_value(req, "pid", val, sizeof(val)) && val[0] != '\0') {
        pid = atoi(val);
    }
    if (ctl_query_value(req, "max", val, sizeof(val)) && val[0] != '\0') {
        max_ms = atoi(val);
    }
    fmrb_app_info_t list[FMRB_MAX_APPS];
    int st[FMRB_MAX_APPS];
    int32_t m = fmrb_app_gc_all(pid, max_ms, NULL, list, st);
    if (pid >= 0 && m == 0) {
        return ctl_json(req, "404 Not Found", "{\"ok\":false,\"err\":\"no such app\"}");
    }

    char body[768];
    size_t off = 0;
    off += (size_t)snprintf(body + off, sizeof(body) - off, "{\"ok\":true,\"apps\":[");
    for (int32_t i = 0; i < m && off < sizeof(body); i++) {
        off += (size_t)snprintf(body + off, sizeof(body) - off,
                                "%s{\"pid\":%d,\"name\":\"%s\",\"status\":\"%s\"}",
                                i ? "," : "", (int)list[i].app_id, list[i].app_name,
                                fmrb_app_gc_status_name(st[i]));
    }
    if (off < sizeof(body)) {
        off += (size_t)snprintf(body + off, sizeof(body) - off, "]}");
    }
    body[sizeof(body) - 1] = '\0';
    return ctl_json(req, "200 OK", body);
}

// Ask the desktop to read /app again, the way the launcher's Reload does:
// after files were sent over (tools/fmrb_rd_fs.rb sync, the app_sync flash)
// the launcher still shows the list it built at boot, so changed .app.toml
// files and icons did not show until a restart. The kernel passes the same
// "apps_installed" note the transfer app sends on to the desktop, which
// rescans now when the launcher is open, else when it next opens. The
// message lives in .rodata; the kernel's queue taking it is all this waits
// for.
static esp_err_t app_rescan_handler(httpd_req_t *req)
{
    // {"cmd" => "apps_installed", "name" => "remote sync"} in MessagePack.
    static const char k_note[] =
        "\x82" "\xa3" "cmd" "\xae" "apps_installed" "\xa4" "name" "\xab" "remote sync";
    fmrb_msg_t msg = {
        .type = FMRB_MSG_TYPE_APP_CONTROL,
        .src_pid = PROC_ID_HOST,
        .size = (uint32_t)(sizeof(k_note) - 1),
    };
    memcpy(msg.data, k_note, sizeof(k_note) - 1);
    if (fmrb_msg_send(PROC_ID_KERNEL, &msg, 500) != FMRB_OK) {
        FMRB_LOGW(TAG, "dev ctl: rescan not sent (the kernel's queue did not take it)");
        return ctl_json(req, "503 Service Unavailable",
                        "{\"ok\":false,\"err\":\"kernel busy\"}");
    }
    FMRB_LOGI(TAG, "dev ctl: asked the launcher to rescan /app");
    return ctl_json(req, "200 OK", "{\"ok\":true}");
}

size_t devctl_status_ident(char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
#if defined(FMRB_HW_TAB5)
    const char *board = "tab5";
#elif defined(FMRB_HW_NARYAV4)
    const char *board = "naryav4";
#elif defined(FMRB_HW_ATOM_DISPLAY)
    const char *board = "atom_display";
#elif defined(FMRB_HW_ATOMS3R)
    const char *board = "atoms3r";
#elif defined(FMRB_HW_STICKS3)
    const char *board = "sticks3";
#else
    const char *board = "narya_v3";
#endif
    char host[40];
    wifi_get_hostname(host, sizeof(host));
    // guard: "free" (a development build: no pairing), "closed" or "open"
    // (a release build, Config > Remote control), so a tool can say which
    // step is missing before it asks for anything.
    fmrb_rguard_state_t gs = fmrb_rguard_state();
    const char *guard = gs == FMRB_RGUARD_NONE ? "free" : gs == FMRB_RGUARD_OPEN ? "open" : "closed";
    int n = snprintf(out, cap, ",\"board\":\"%s\",\"host\":\"%s\",\"guard\":\"%s\"",
                     board, host, guard);
    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    size_t len = (size_t)n < cap ? (size_t)n : cap - 1;
#if defined(FMRB_HW_MODERN)
    // The partitions an app-only flash relies on and the font image in its
    // partition (doc/flash_layout F4): rake flash:app_sync compares them with
    // the build and stops when a full flash is needed.
    len += font_ja_flash_status(out + len, cap - len);
#endif
    return len;
}

// The machine's mute and volume (doc/reference/audio_output.md). Here so a development loop
// can silence the board before it launches anything, or before a reflash: the
// settings are saved, so the boot after the flash obeys them too. Every
// answer carries both, so a caller always sees the whole state.
static esp_err_t audio_state_reply(httpd_req_t *req)
{
    char body[64];
    snprintf(body, sizeof(body), "{\"ok\":true,\"muted\":%s,\"volume\":%d}",
             fmrb_host_audio_muted() ? "true" : "false", fmrb_host_audio_volume());
    return ctl_json(req, "200 OK", body);
}

static esp_err_t audio_get_handler(httpd_req_t *req)
{
    return audio_state_reply(req);
}

static esp_err_t audio_mute_post_handler(httpd_req_t *req)
{
    char on[8];
    if (!ctl_query_value(req, "on", on, sizeof(on)) ||
        (strcmp(on, "0") != 0 && strcmp(on, "1") != 0)) {
        return ctl_json(req, "400 Bad Request",
                        "{\"ok\":false,\"err\":\"on=0 or on=1 required\"}");
    }
    fmrb_host_set_audio_mute(on[0] == '1');
    FMRB_LOGI(TAG, "dev ctl: audio mute %s", on[0] == '1' ? "on" : "off");
    return audio_state_reply(req);
}

static esp_err_t audio_volume_post_handler(httpd_req_t *req)
{
    char level[8];
    char *end = NULL;
    long v = -1;
    if (ctl_query_value(req, "level", level, sizeof(level)) && level[0]) {
        v = strtol(level, &end, 10);
    }
    if (v < 0 || v > 10 || !end || *end != '\0') {
        return ctl_json(req, "400 Bad Request",
                        "{\"ok\":false,\"err\":\"level=0..10 required\"}");
    }
    fmrb_host_set_audio_volume((int)v);
    FMRB_LOGI(TAG, "dev ctl: audio volume %ld", v);
    return audio_state_reply(req);
}

#ifdef FMRB_HAVE_CHECKPOINT
// ---------------------------------------------------------------
// Moving an app (doc/checkpoint F6): the debug server's checkpoint and move,
// for a board whose debug server is BLE only. They ask and answer at once;
// GET /app/checkpoint?pid= tells how it went (fmrb_app_checkpoint_poll), so
// the httpd task never waits for an app or for another machine.
// ---------------------------------------------------------------

static int32_t ctl_query_int(httpd_req_t *req, const char *key, int32_t dflt)
{
    char v[16];
    if (!ctl_query_value(req, key, v, sizeof(v)) || v[0] == '\0') return dflt;
    return (int32_t)atol(v);
}

static esp_err_t ctl_ckpt_answer(httpd_req_t *req, fmrb_err_t err, const char *what)
{
    char body[96];
    if (err != FMRB_OK) {
        snprintf(body, sizeof(body), "{\"ok\":false,\"err\":%d}", (int)err);
        FMRB_LOGW(TAG, "dev ctl: %s failed (%d)", what, (int)err);
        return ctl_json(req, "500 Internal Server Error", body);
    }
    FMRB_LOGI(TAG, "dev ctl: %s", what);
    return ctl_json(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t checkpoint_post_handler(httpd_req_t *req)
{
    int32_t pid = ctl_query_int(req, "pid", -1);
    if (pid < 0) return ctl_json(req, "400 Bad Request", "{\"ok\":false,\"err\":\"pid required\"}");
    bool restart = ctl_query_int(req, "restart", 0) != 0;
    int32_t hold = ctl_query_int(req, "hold", 0);
    char what[64];
    snprintf(what, sizeof(what), "checkpoint pid %d%s", (int)pid, restart ? " (restart)" : "");
    return ctl_ckpt_answer(req, fmrb_app_checkpoint_ask(pid, restart, hold), what);
}

static esp_err_t move_handler(httpd_req_t *req)
{
    int32_t pid = ctl_query_int(req, "pid", -1);
    char node[64];
    if (pid < 0 || !ctl_query_value(req, "node", node, sizeof(node)) || node[0] == '\0') {
        return ctl_json(req, "400 Bad Request", "{\"ok\":false,\"err\":\"pid and node required\"}");
    }
    int32_t hold = ctl_query_int(req, "hold", 0);
    char what[112];
    snprintf(what, sizeof(what), "move pid %d to %s", (int)pid, node);
    return ctl_ckpt_answer(req, fmrb_app_checkpoint_move(pid, node, hold), what);
}

static const char *ctl_ckpt_status_name(fmrb_app_ckpt_status_t st)
{
    switch (st) {
    case FMRB_APP_CKPT_NONE:      return "none";
    case FMRB_APP_CKPT_PENDING:   return "pending";
    case FMRB_APP_CKPT_WRITTEN:   return "written";
    case FMRB_APP_CKPT_REFUSED:   return "refused";
    case FMRB_APP_CKPT_HELD:      return "held";
    case FMRB_APP_CKPT_CLOSED:    return "closed";
    case FMRB_APP_CKPT_CONTINUED: return "continued";
    default:                      return "unknown";
    }
}

static esp_err_t checkpoint_get_handler(httpd_req_t *req)
{
    int32_t pid = ctl_query_int(req, "pid", -1);
    if (pid < 0) return ctl_json(req, "400 Bad Request", "{\"ok\":false,\"err\":\"pid required\"}");
    char what[256];
    what[0] = '\0';
    fmrb_app_ckpt_status_t st = fmrb_app_checkpoint_poll(pid, what, sizeof(what));
    // what is a path or a reason: plain text, escaped for JSON here
    char esc[sizeof(what) * 2];
    size_t o = 0;
    for (const char *c = what; *c && o + 2 < sizeof(esc); c++) {
        unsigned char ch = (unsigned char)*c;
        if (ch == '"' || ch == '\\') { esc[o++] = '\\'; esc[o++] = (char)ch; }
        else if (ch >= 0x20) esc[o++] = (char)ch;
    }
    esc[o] = '\0';
    char body[sizeof(esc) + 64];
    snprintf(body, sizeof(body), "{\"ok\":true,\"status\":\"%s\",\"what\":\"%s\"}",
             ctl_ckpt_status_name(st), esc);
    return ctl_json(req, "200 OK", body);
}
#endif /* FMRB_HAVE_CHECKPOINT */

// Register and say so when it does not take. httpd_register_uri_handler
// returns ESP_ERR_HTTPD_HANDLERS_FULL rather than asserting, so ignoring it
// leaves a server that starts cleanly and 404s the route just added.
// Every route goes through the guard (guarded_handler); the server copies the
// route, so the one built here on the stack is enough.
static void devctl_register_uri(httpd_handle_t server, const httpd_uri_t *uri)
{
    httpd_uri_t g = *uri;
    g.handler = guarded_handler;
    g.user_ctx = (void *)uri;
    esp_err_t err = httpd_register_uri_handler(server, &g);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "could not register %s: %d (raise max_uri_handlers?)",
                  uri->uri, err);
    }
}

fmrb_err_t devctl_http_register(httpd_handle_t server)
{
    if (!server) return FMRB_ERR_INVALID_PARAM;
#if CONFIG_IDF_TARGET_ESP32S3
    if (!s_fs_buf) {
        s_fs_buf = heap_caps_malloc(RD_FS_IO_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s_fs_buf) {
            FMRB_LOGE(TAG, "no memory for the file transfer buffer");
            return FMRB_ERR_NO_MEMORY;
        }
    }
#endif
    if (!devctl_fs_unrestricted()) devctl_guard_arm(server);
    static const httpd_uri_t uri_launch = {
        .uri = "/app/launch", .method = HTTP_POST, .handler = launch_handler };
    static const httpd_uri_t uri_kill = {
        .uri = "/app/kill", .method = HTTP_POST, .handler = kill_handler };
    static const httpd_uri_t uri_list = {
        .uri = "/app/list", .method = HTTP_GET, .handler = list_handler };
    static const httpd_uri_t uri_fs_list = {
        .uri = "/fs/list", .method = HTTP_GET, .handler = fs_list_handler };
    static const httpd_uri_t uri_fs_get = {
        .uri = "/fs/get", .method = HTTP_GET, .handler = fs_get_handler };
    static const httpd_uri_t uri_fs_put = {
        .uri = "/fs/put", .method = HTTP_PUT, .handler = fs_put_handler };
    static const httpd_uri_t uri_fs_del = {
        .uri = "/fs/del", .method = HTTP_DELETE, .handler = fs_del_handler };
    static const httpd_uri_t uri_fs_mkdir = {
        .uri = "/fs/mkdir", .method = HTTP_POST, .handler = fs_mkdir_handler };
    static const httpd_uri_t uri_fs_md5 = {
        .uri = "/fs/md5", .method = HTTP_GET, .handler = fs_md5_handler };
    static const httpd_uri_t uri_fs_df = {
        .uri = "/fs/df", .method = HTTP_GET, .handler = fs_df_handler };
    static const httpd_uri_t uri_sys_reboot = {
        .uri = "/sys/reboot", .method = HTTP_POST, .handler = sys_reboot_handler };
    static const httpd_uri_t uri_sys_gc = {
        .uri = "/sys/gc", .method = HTTP_POST, .handler = sys_gc_handler };
    static const httpd_uri_t uri_app_rescan = {
        .uri = "/app/rescan", .method = HTTP_POST, .handler = app_rescan_handler };
    static const httpd_uri_t uri_audio_mute_get = {
        .uri = "/audio/mute", .method = HTTP_GET, .handler = audio_get_handler };
    static const httpd_uri_t uri_audio_mute_post = {
        .uri = "/audio/mute", .method = HTTP_POST, .handler = audio_mute_post_handler };
    static const httpd_uri_t uri_audio_volume_get = {
        .uri = "/audio/volume", .method = HTTP_GET, .handler = audio_get_handler };
    static const httpd_uri_t uri_audio_volume_post = {
        .uri = "/audio/volume", .method = HTTP_POST, .handler = audio_volume_post_handler };
    // Open to everyone: how a PC gets its token, and how it checks one.
    static const httpd_uri_t uri_pair = {
        .uri = "/pair", .method = HTTP_POST, .handler = pair_handler };
    static const httpd_uri_t uri_auth = {
        .uri = "/auth", .method = HTTP_GET, .handler = auth_handler };
    esp_err_t perr = httpd_register_uri_handler(server, &uri_pair);
    if (perr == ESP_OK) perr = httpd_register_uri_handler(server, &uri_auth);
    if (perr != ESP_OK) {
        FMRB_LOGE(TAG, "could not register /pair, /auth: %d (raise max_uri_handlers?)", perr);
    }
    devctl_register_uri(server, &uri_launch);
    devctl_register_uri(server, &uri_kill);
    devctl_register_uri(server, &uri_list);
    devctl_register_uri(server, &uri_fs_list);
    devctl_register_uri(server, &uri_fs_get);
    devctl_register_uri(server, &uri_fs_put);
    devctl_register_uri(server, &uri_fs_del);
    devctl_register_uri(server, &uri_fs_mkdir);
    devctl_register_uri(server, &uri_fs_md5);
    devctl_register_uri(server, &uri_fs_df);
    devctl_register_uri(server, &uri_sys_reboot);
    devctl_register_uri(server, &uri_sys_gc);
    devctl_register_uri(server, &uri_app_rescan);
    devctl_register_uri(server, &uri_audio_mute_get);
    devctl_register_uri(server, &uri_audio_mute_post);
    devctl_register_uri(server, &uri_audio_volume_get);
    devctl_register_uri(server, &uri_audio_volume_post);
#ifdef FMRB_HAVE_CHECKPOINT
    static const httpd_uri_t uri_ckpt_post = {
        .uri = "/app/checkpoint", .method = HTTP_POST, .handler = checkpoint_post_handler };
    static const httpd_uri_t uri_ckpt_get = {
        .uri = "/app/checkpoint", .method = HTTP_GET, .handler = checkpoint_get_handler };
    static const httpd_uri_t uri_move = {
        .uri = "/app/move", .method = HTTP_POST, .handler = move_handler };
    devctl_register_uri(server, &uri_ckpt_post);
    devctl_register_uri(server, &uri_ckpt_get);
    devctl_register_uri(server, &uri_move);
#endif
    if (devctl_fs_unrestricted()) {
        FMRB_LOGW(TAG, "development remote control is enabled (/app/launch, /app/kill, /app/list, /app/rescan, /fs/*, /audio/*, /sys/reboot, /sys/gc); "
                  "the file endpoints reach every path, keys included");
    } else {
        FMRB_LOGI(TAG, "remote control is %s (Config > Remote control, %d PCs paired); "
                  "files are written only under /home, /app and /mnt/sd",
                  fmrb_rguard_state() == FMRB_RGUARD_OPEN ? "on" : "off",
                  fmrb_rguard_paired());
    }
    return FMRB_OK;
}
