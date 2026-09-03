// Development remote control over HTTP: launch, kill and list apps, and move
// files, so a development loop does not have to drive the launcher through
// synthetic clicks.
//
//   POST /app/launch?path=   GET /app/list        POST /app/kill?pid=
//   GET  /fs/list?path=      GET  /fs/get?path=   PUT  /fs/put?path=
//   DELETE /fs/del?path=     POST /fs/mkdir?path=
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
// This is unauthenticated, like the remote desktop, and compiled in only when
// FMRB_DEV_REMOTE_CTL is defined -- a release build does not carry it.
// Plan and history: doc/dev_remote_ctl/plan.md.

#include "devctl_http.h"

#include "fmrb_app.h"
#include "fmrb_log.h"
#include "fmrb_hal_file.h"

#include "esp_http_server.h"

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
// This is a control plane for development and is unauthenticated, like the
// rest of the remote desktop. It opens no door that was not already open --
// anyone who can reach the viewer can drive the launcher and start anything --
// but it is compiled out unless FMRB_DEV_REMOTE_CTL is defined, so a release
// build does not carry it.
//
// Called straight from the httpd task, the way debugd calls the same functions
// from its own. That task has 8KB of stack against debugd's 6KB, so spawning
// from here is no tighter than spawning from there.
// ---------------------------------------------------------------

#define RD_CTL_QUERY_MAX 192
#define RD_CTL_PATH_MAX  128

static esp_err_t ctl_json(httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

// One key out of the request's query string. Bad input answers with JSON
// rather than dropping the connection: a development tool should be told what
// it got wrong.
static bool ctl_query_value(httpd_req_t *req, const char *key, char *out, size_t out_len)
{
    char q[RD_CTL_QUERY_MAX];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(q, key, out, out_len) == ESP_OK;
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
// Paths are the ones apps use (/app, /home, /usr/share, /mnt/sd) and go
// through the HAL resolver, the same as File does. Only those four roots are
// reachable, and a ".." segment is refused before resolving; /etc stays out
// of reach because a broken system_conf is a broken boot. Same standing as
// the control endpoints above: unauthenticated, development only, compiled
// out of a release.
//
// Transfers stream through one static 4KB buffer rather than the httpd task's
// 8KB stack. A put writes to "<path>.part" and renames at the end so a dropped
// connection leaves no half-written file under the real name.
// ---------------------------------------------------------------

#define RD_FS_VPATH_MAX 192
#define RD_FS_RPATH_MAX 256
#define RD_FS_IO_CHUNK  4096

static uint8_t s_fs_buf[RD_FS_IO_CHUNK];

static const char *const s_fs_roots[] = { "/mnt/sd", "/home", "/app", "/usr/share" };

// Reject anything outside the allowed roots or containing a ".." segment.
static bool fs_path_allowed(const char *vpath)
{
    if (vpath[0] != '/') return false;
    for (const char *p = vpath; *p; p++) {
        if (p[0] == '.' && p[1] == '.' &&
            (p[2] == '\0' || p[2] == '/') && (p == vpath || p[-1] == '/')) {
            return false;
        }
    }
    for (size_t i = 0; i < sizeof(s_fs_roots) / sizeof(s_fs_roots[0]); i++) {
        size_t n = strlen(s_fs_roots[i]);
        if (strncmp(vpath, s_fs_roots[i], n) == 0 &&
            (vpath[n] == '\0' || vpath[n] == '/')) {
            return true;
        }
    }
    return false;
}

// Undo %XX in place. httpd_query_key_value hands the value back still
// percent-encoded, and any client that encodes properly sends "/" as %2F --
// which would then fail the root check below.
static void fs_url_decode(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            char hex[3] = { r[1], r[2], 0 };
            *w++ = (char)strtol(hex, NULL, 16);
            r += 2;
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

// Query "path" -> checked virtual path and resolved real path. Answers the
// request itself on failure so the handlers can just return.
static bool fs_req_path(httpd_req_t *req, char *vpath, size_t vlen,
                        char *rpath, size_t rlen)
{
    if (!ctl_query_value(req, "path", vpath, vlen) || vpath[0] == '\0') {
        ctl_json(req, "400 Bad Request", "{\"ok\":false,\"err\":\"path required\"}");
        return false;
    }
    fs_url_decode(vpath);
    if (!fs_path_allowed(vpath)) {
        ctl_json(req, "400 Bad Request",
                 "{\"ok\":false,\"err\":\"path outside /mnt/sd, /home, /app, /usr/share\"}");
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
    if (!fs_req_path(req, vpath, sizeof(vpath), rpath, sizeof(rpath))) return ESP_OK;

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

static esp_err_t fs_get_handler(httpd_req_t *req)
{
    char vpath[RD_FS_VPATH_MAX];
    char rpath[RD_FS_RPATH_MAX];
    if (!fs_req_path(req, vpath, sizeof(vpath), rpath, sizeof(rpath))) return ESP_OK;

    FILE *f = fopen(rpath, "rb");
    if (!f) {
        return ctl_json(req, "404 Not Found", "{\"ok\":false,\"err\":\"no such file\"}");
    }
    httpd_resp_set_type(req, "application/octet-stream");
    size_t total = 0;
    size_t n;
    while ((n = fread(s_fs_buf, 1, sizeof(s_fs_buf), f)) > 0) {
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
    if (!fs_req_path(req, vpath, sizeof(vpath), rpath, sizeof(rpath))) return ESP_OK;

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
        size_t want = remaining < sizeof(s_fs_buf) ? remaining : sizeof(s_fs_buf);
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
    unlink(rpath);  // rename does not replace on every filesystem here
    if (rename(tmp, rpath) != 0) {
        unlink(tmp);
        return ctl_json(req, "500 Internal Server Error",
                        "{\"ok\":false,\"err\":\"rename failed\"}");
    }
    char body[64];
    snprintf(body, sizeof(body), "{\"ok\":true,\"size\":%u}", (unsigned)total);
    FMRB_LOGI(TAG, "dev fs: put %s (%u bytes)", vpath, (unsigned)total);
    return ctl_json(req, "200 OK", body);
}

static esp_err_t fs_del_handler(httpd_req_t *req)
{
    char vpath[RD_FS_VPATH_MAX];
    char rpath[RD_FS_RPATH_MAX];
    if (!fs_req_path(req, vpath, sizeof(vpath), rpath, sizeof(rpath))) return ESP_OK;

    struct stat st;
    if (stat(rpath, &st) != 0) {
        return ctl_json(req, "404 Not Found", "{\"ok\":false,\"err\":\"no such file\"}");
    }
    int rc = S_ISDIR(st.st_mode) ? rmdir(rpath) : unlink(rpath);
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
    if (!fs_req_path(req, vpath, sizeof(vpath), rpath, sizeof(rpath))) return ESP_OK;

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

// Register and say so when it does not take. httpd_register_uri_handler
// returns ESP_ERR_HTTPD_HANDLERS_FULL rather than asserting, so ignoring it
// leaves a server that starts cleanly and 404s the route just added.
static void devctl_register_uri(httpd_handle_t server, const httpd_uri_t *uri)
{
    esp_err_t err = httpd_register_uri_handler(server, uri);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "could not register %s: %d (raise max_uri_handlers?)",
                  uri->uri, err);
    }
}

fmrb_err_t devctl_http_register(httpd_handle_t server)
{
    if (!server) return FMRB_ERR_INVALID_PARAM;
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
    devctl_register_uri(server, &uri_launch);
    devctl_register_uri(server, &uri_kill);
    devctl_register_uri(server, &uri_list);
    devctl_register_uri(server, &uri_fs_list);
    devctl_register_uri(server, &uri_fs_get);
    devctl_register_uri(server, &uri_fs_put);
    devctl_register_uri(server, &uri_fs_del);
    devctl_register_uri(server, &uri_fs_mkdir);
    FMRB_LOGW(TAG, "development remote control is enabled (/app/launch, /app/kill, /app/list, /fs/*)");
    return FMRB_OK;
}
