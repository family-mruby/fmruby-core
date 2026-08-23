// Remote-desktop HTTP/WebSocket server (Phase 1: MJPEG + input).
//
// - "/"           embedded viewer page (tool/web/remote/)
// - "/stream"     MJPEG via multipart/x-mixed-replace; one client at a
//                 time (a second request gets 503). The URI handler
//                 detaches the request (httpd_req_async_handler_begin)
//                 and a dedicated task runs the capture->encode->send
//                 loop: esp_http_server is single-task, so looping in
//                 the handler would starve the WS input frames and the
//                 queued cursor pushes for as long as a client streams.
// - "/ws"         WebSocket: binary input messages (rd_input) from the
//                 browser; JSON cursor/info messages to the browser.
// - "/status"     small JSON status document.
// - "/app/launch" "/app/kill" "/app/list"
//                 development remote control, compiled in only with
//                 FMRB_DEV_REMOTE_CTL (doc/dev_remote_ctl/plan.md).
//
// The httpd instance runs on core 0 next to the hosted/lwIP tasks.

#include "rd_http.h"
#include "rd_encoder_jpeg.h"
#include "rd_input.h"
#include "rd_stream.h"

#include "fmrb_app.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include "display_p4_task.h"
#include "wifi_task.h"

#include "esp_http_server.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "fmrb_task_config.h"

static const char *TAG = "rd_http";

// Embedded viewer assets (EMBED_FILES in main/CMakeLists.txt)
extern const uint8_t rd_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t rd_index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t rd_remote_js_start[]  asm("_binary_remote_js_start");
extern const uint8_t rd_remote_js_end[]    asm("_binary_remote_js_end");
extern const uint8_t rd_keymap_js_start[]  asm("_binary_keymap_js_start");
extern const uint8_t rd_keymap_js_end[]    asm("_binary_keymap_js_end");

#define RD_WS_MAX_CLIENTS   4
#define RD_CURSOR_PERIOD_MS 100

static httpd_handle_t s_server = NULL;
static rd_http_config_t s_cfg;
static atomic_bool s_stream_busy = false;
static atomic_bool s_stop = false;

// Streaming statistics (updated by the stream loop, read by /status)
static volatile uint32_t s_stat_fps_x10 = 0;
static volatile uint32_t s_stat_kbps = 0;

// WS client registry for async pushes (cursor metadata)
static int s_ws_fds[RD_WS_MAX_CLIENTS];
static portMUX_TYPE s_ws_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_timer_handle_t s_cursor_timer = NULL;
static int s_last_cur_x = -1, s_last_cur_y = -1;
static bool s_last_cur_v = false;

// ---------------------------------------------------------------
// Static assets
// ---------------------------------------------------------------

static esp_err_t asset_handler(httpd_req_t *req)
{
    const uint8_t *start, *end;
    const char *type;
    if (strcmp(req->uri, "/remote.js") == 0) {
        start = rd_remote_js_start; end = rd_remote_js_end;
        type = "application/javascript";
    } else if (strcmp(req->uri, "/keymap.js") == 0) {
        start = rd_keymap_js_start; end = rd_keymap_js_end;
        type = "application/javascript";
    } else {
        start = rd_index_html_start; end = rd_index_html_end;
        type = "text/html";
    }
    httpd_resp_set_type(req, type);
    return httpd_resp_send(req, (const char *)start, end - start);
}

// ---------------------------------------------------------------
// MJPEG stream
// ---------------------------------------------------------------

#define RD_BOUNDARY "fmrbframe"

// Runs on its own task: the request has been detached from the httpd
// task with httpd_req_async_handler_begin() so WS input and cursor
// pushes keep flowing while the stream is live.
static void mjpeg_stream_task(void *arg)
{
    httpd_req_t *req = (httpd_req_t *)arg;

    FMRB_LOGI(TAG, "MJPEG client connected");
    display_p4_capture_enable(true);
    display_p4_capture_kick();

    httpd_resp_set_type(req,
        "multipart/x-mixed-replace; boundary=" RD_BOUNDARY);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    const uint32_t frame_interval_ms =
        (s_cfg.fps_cap > 0) ? (1000u / s_cfg.fps_cap) : 66u;
    uint32_t last_seq = 0;
    uint32_t frames = 0, bytes = 0;
    int64_t stat_t0 = esp_timer_get_time();

    while (!atomic_load(&s_stop)) {
        int64_t t_frame = esp_timer_get_time();

        // Wait for a newer frame; on timeout resend the latest one as a
        // keepalive (rendering is event-driven and idles when static)
        display_p4_capture_frame_t frame;
        fmrb_err_t err = display_p4_capture_acquire(last_seq + 1, 500, &frame);
        if (err == FMRB_ERR_TIMEOUT) {
            err = display_p4_capture_acquire(0, 100, &frame);
        }
        if (err != FMRB_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        last_seq = frame.seq;

        // Hold the encoder from the encode until the last byte of it has
        // gone out: the output buffer is the encoder's one and only, and
        // EXPORT_FRAME wants it too.
        const uint8_t *jpeg = NULL;
        size_t jpeg_len = 0;
        if (rd_encoder_jpeg_lock(2000) != FMRB_OK) {
            display_p4_capture_release();
            continue;
        }
        err = rd_encoder_jpeg_encode(frame.pixels, &jpeg, &jpeg_len);
        display_p4_capture_release();
        if (err != FMRB_OK) {
            rd_encoder_jpeg_unlock();
            break;
        }

        char part_hdr[128];
        int hdr_len = snprintf(part_hdr, sizeof(part_hdr),
                               "--" RD_BOUNDARY "\r\n"
                               "Content-Type: image/jpeg\r\n"
                               "Content-Length: %u\r\n"
                               "X-Seq: %u\r\n\r\n",
                               (unsigned)jpeg_len, (unsigned)frame.seq);
        bool sent = (httpd_resp_send_chunk(req, part_hdr, hdr_len) == ESP_OK &&
                     httpd_resp_send_chunk(req, (const char *)jpeg, jpeg_len) == ESP_OK &&
                     httpd_resp_send_chunk(req, "\r\n", 2) == ESP_OK);
        rd_encoder_jpeg_unlock();
        if (!sent) {
            break;  // client disconnected
        }

        frames++;
        bytes += jpeg_len;
        int64_t now = esp_timer_get_time();
        if (now - stat_t0 >= 2000000) {
            uint32_t ms = (uint32_t)((now - stat_t0) / 1000);
            s_stat_fps_x10 = frames * 10000u / ms;
            s_stat_kbps = (uint32_t)((uint64_t)bytes * 8u / ms);
            frames = 0; bytes = 0;
            stat_t0 = now;
        }

        // Pace to fps_cap
        uint32_t spent_ms = (uint32_t)((now - t_frame) / 1000);
        if (spent_ms < frame_interval_ms) {
            vTaskDelay(pdMS_TO_TICKS(frame_interval_ms - spent_ms));
        }
    }

    display_p4_capture_enable(false);
    s_stat_fps_x10 = 0;
    s_stat_kbps = 0;
    FMRB_LOGI(TAG, "MJPEG client disconnected");
    httpd_req_async_handler_complete(req);
    atomic_store(&s_stream_busy, false);
    fmrb_task_delete_ex(NULL);
}

int rd_http_stream_state(void)
{
    if (rd_stream_has_clients())
        return 2;
    if (atomic_load(&s_stream_busy))
        return 1;
    return 0;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_stream_busy, &expected, true)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "stream busy (one client at a time)", -1);
        return ESP_OK;
    }

    httpd_req_t *async_req = NULL;
    if (httpd_req_async_handler_begin(req, &async_req) != ESP_OK) {
        atomic_store(&s_stream_busy, false);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
        return ESP_FAIL;
    }
    if (xTaskCreatePinnedToCore(mjpeg_stream_task, "rd_mjpeg",
                                FMRB_RD_MJPEG_TASK_STACK_SIZE, async_req,
                                FMRB_RD_MJPEG_TASK_PRIORITY, NULL,
                                FMRB_RD_MJPEG_TASK_CORE) != pdPASS) {
        FMRB_LOGE(TAG, "failed to create mjpeg task");
        httpd_req_async_handler_complete(async_req);
        atomic_store(&s_stream_busy, false);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// ---------------------------------------------------------------
// WebSocket: input + cursor metadata
// ---------------------------------------------------------------

static void ws_register_fd(int fd)
{
    portENTER_CRITICAL(&s_ws_lock);
    for (int i = 0; i < RD_WS_MAX_CLIENTS; i++) {
        if (s_ws_fds[i] == fd) goto out;
    }
    for (int i = 0; i < RD_WS_MAX_CLIENTS; i++) {
        if (s_ws_fds[i] < 0) { s_ws_fds[i] = fd; break; }
    }
out:
    portEXIT_CRITICAL(&s_ws_lock);
}

static void ws_unregister_fd(int fd)
{
    portENTER_CRITICAL(&s_ws_lock);
    for (int i = 0; i < RD_WS_MAX_CLIENTS; i++) {
        if (s_ws_fds[i] == fd) s_ws_fds[i] = -1;
    }
    portEXIT_CRITICAL(&s_ws_lock);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // Handshake done: greet with stream geometry
        ws_register_fd(httpd_req_to_sockfd(req));
        char info[112];
        int n = snprintf(info, sizeof(info),
                         "{\"t\":\"info\",\"w\":426,\"h\":240,"
                         "\"encw\":%u,\"h264\":%s}",
                         (unsigned)rd_encoder_jpeg_width(),
                         s_cfg.h264_enable ? "true" : "false");
        httpd_ws_frame_t f = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)info,
            .len = (size_t)n,
        };
        httpd_ws_send_frame(req, &f);
        FMRB_LOGI(TAG, "WS client connected (fd=%d)", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t frame = { 0 };
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) return err;

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        ws_unregister_fd(httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    if (frame.len == 0 || frame.len > 64) {
        return ESP_OK;  // ignore empty/oversized frames
    }

    uint8_t buf[64];
    frame.payload = buf;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) return err;

    if (frame.type == HTTPD_WS_TYPE_BINARY) {
        if (buf[0] == RD_INPUT_MSG_KEYFRAME_REQ) {
            rd_stream_request_idr();
        } else {
            rd_input_handle(buf, frame.len);
        }
    }
    return ESP_OK;
}

// H.264 video WebSocket: connect registers the client with the streamer;
// inbound frames are only keyframe requests / close.
static esp_err_t ws_video_handler(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    if (req->method == HTTP_GET) {
        rd_stream_add_client(fd);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = { 0 };
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        rd_stream_remove_client(fd);
        return err;
    }
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        rd_stream_remove_client(fd);
        return ESP_OK;
    }
    if (frame.len == 0 || frame.len > 16) return ESP_OK;

    uint8_t buf[16];
    frame.payload = buf;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) return err;
    if (frame.type == HTTPD_WS_TYPE_BINARY &&
        buf[0] == RD_INPUT_MSG_KEYFRAME_REQ) {
        rd_stream_request_idr();
    }
    return ESP_OK;
}

// Cursor push: esp_timer -> httpd work queue -> async WS send
typedef struct {
    char json[64];
    size_t len;
} cursor_msg_t;

static void cursor_send_work(void *arg)
{
    cursor_msg_t *msg = (cursor_msg_t *)arg;
    int fds[RD_WS_MAX_CLIENTS];
    portENTER_CRITICAL(&s_ws_lock);
    memcpy(fds, s_ws_fds, sizeof(fds));
    portEXIT_CRITICAL(&s_ws_lock);

    for (int i = 0; i < RD_WS_MAX_CLIENTS; i++) {
        if (fds[i] < 0) continue;
        httpd_ws_frame_t f = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)msg->json,
            .len = msg->len,
        };
        if (httpd_ws_send_frame_async(s_server, fds[i], &f) != ESP_OK) {
            ws_unregister_fd(fds[i]);
        }
    }
    free(msg);
}

static void cursor_timer_cb(void *arg)
{
    (void)arg;
    if (!s_server) return;

    bool any;
    portENTER_CRITICAL(&s_ws_lock);
    any = false;
    for (int i = 0; i < RD_WS_MAX_CLIENTS; i++) {
        if (s_ws_fds[i] >= 0) { any = true; break; }
    }
    portEXIT_CRITICAL(&s_ws_lock);
    if (!any) return;

    int x, y;
    bool v;
    display_p4_get_cursor(&x, &y, &v);
    if (x == s_last_cur_x && y == s_last_cur_y && v == s_last_cur_v) return;
    s_last_cur_x = x; s_last_cur_y = y; s_last_cur_v = v;

    cursor_msg_t *msg = malloc(sizeof(cursor_msg_t));
    if (!msg) return;
    msg->len = (size_t)snprintf(msg->json, sizeof(msg->json),
                                "{\"t\":\"cur\",\"x\":%d,\"y\":%d,\"v\":%d}",
                                x, y, v ? 1 : 0);
    if (httpd_queue_work(s_server, cursor_send_work, msg) != ESP_OK) {
        free(msg);
    }
}

// ---------------------------------------------------------------
// Status
// ---------------------------------------------------------------

static esp_err_t status_handler(httpd_req_t *req)
{
    char ip[16];
    wifi_get_ip_str(ip, sizeof(ip));
    char body[160];
    int n = snprintf(body, sizeof(body),
                     "{\"ip\":\"%s\",\"mode\":\"mjpeg\",\"streaming\":%s,"
                     "\"fps\":%u.%u,\"kbps\":%u}",
                     ip, atomic_load(&s_stream_busy) ? "true" : "false",
                     (unsigned)(s_stat_fps_x10 / 10),
                     (unsigned)(s_stat_fps_x10 % 10),
                     (unsigned)s_stat_kbps);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, n);
}

#ifdef FMRB_DEV_REMOTE_CTL
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
    // hanging depending on which task asks (doc/app_kill_fix) -- keeping to the
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
#endif /* FMRB_DEV_REMOTE_CTL */

// ---------------------------------------------------------------
// Server lifecycle
// ---------------------------------------------------------------

// Register and say so when it does not take. httpd_register_uri_handler
// returns ESP_ERR_HTTPD_HANDLERS_FULL rather than asserting, so ignoring it
// leaves a server that starts cleanly and 404s the route you just added.
static void rd_register_uri(const httpd_uri_t *uri)
{
    esp_err_t err = httpd_register_uri_handler(s_server, uri);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "could not register %s: %d (raise max_uri_handlers?)",
                  uri->uri, err);
    }
}

fmrb_err_t rd_http_start(const rd_http_config_t *cfg)
{
    if (s_server) return FMRB_OK;
    if (!cfg) return FMRB_ERR_INVALID_PARAM;
    s_cfg = *cfg;
    atomic_store(&s_stop, false);
    for (int i = 0; i < RD_WS_MAX_CLIENTS; i++) s_ws_fds[i] = -1;

    // Bracketed like every other use of the encoder: EXPORT_FRAME brings it
    // up on demand when the remote desktop was never started, so start and
    // stop are no longer the only ones touching it.
    fmrb_err_t ferr = rd_encoder_jpeg_lock(2000);
    if (ferr != FMRB_OK) return ferr;
    ferr = rd_encoder_jpeg_init(426, 240, s_cfg.jpeg_quality);
    rd_encoder_jpeg_unlock();
    if (ferr != FMRB_OK) return ferr;

    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.core_id = FMRB_RD_HTTPD_TASK_CORE;
    hcfg.task_priority = FMRB_RD_HTTPD_TASK_PRIORITY;
    hcfg.stack_size = FMRB_RD_HTTPD_TASK_STACK_SIZE;
    hcfg.max_open_sockets = 7;
    hcfg.lru_purge_enable = true;
    // The default is 8 and the fixed set already uses 7 (index, remote.js,
    // keymap.js, stream, status, ws, ws_video). The dev control endpoints take
    // it over, and httpd_register_uri_handler answers ESP_ERR_HTTPD_HANDLERS_FULL
    // rather than complaining -- so the server came up looking healthy and every
    // one of the new routes returned 404. Sized with room to add more.
    hcfg.max_uri_handlers = 16;

    esp_err_t err = httpd_start(&s_server, &hcfg);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "httpd_start failed: %d", err);
        if (rd_encoder_jpeg_lock(2000) == FMRB_OK) {
            rd_encoder_jpeg_deinit();
            rd_encoder_jpeg_unlock();
        }
        return FMRB_ERR_FAILED;
    }

    static const httpd_uri_t uri_index = {
        .uri = "/", .method = HTTP_GET, .handler = asset_handler };
    static const httpd_uri_t uri_js = {
        .uri = "/remote.js", .method = HTTP_GET, .handler = asset_handler };
    static const httpd_uri_t uri_keymap = {
        .uri = "/keymap.js", .method = HTTP_GET, .handler = asset_handler };
    static const httpd_uri_t uri_stream = {
        .uri = "/stream", .method = HTTP_GET, .handler = stream_handler };
    static const httpd_uri_t uri_status = {
        .uri = "/status", .method = HTTP_GET, .handler = status_handler };
    static const httpd_uri_t uri_ws = {
        .uri = "/ws", .method = HTTP_GET, .handler = ws_handler,
        .is_websocket = true };
    static const httpd_uri_t uri_ws_video = {
        .uri = "/ws_video", .method = HTTP_GET, .handler = ws_video_handler,
        .is_websocket = true };

    rd_register_uri(&uri_index);
    rd_register_uri(&uri_js);
    rd_register_uri(&uri_keymap);
    rd_register_uri(&uri_stream);
    rd_register_uri(&uri_status);
    rd_register_uri(&uri_ws);
#ifdef FMRB_DEV_REMOTE_CTL
    static const httpd_uri_t uri_launch = {
        .uri = "/app/launch", .method = HTTP_POST, .handler = launch_handler };
    static const httpd_uri_t uri_kill = {
        .uri = "/app/kill", .method = HTTP_POST, .handler = kill_handler };
    static const httpd_uri_t uri_list = {
        .uri = "/app/list", .method = HTTP_GET, .handler = list_handler };
    rd_register_uri(&uri_launch);
    rd_register_uri(&uri_kill);
    rd_register_uri(&uri_list);
    FMRB_LOGW(TAG, "development remote control is enabled (/app/launch, /app/kill, /app/list)");
#endif
    if (s_cfg.h264_enable) {
        rd_register_uri(&uri_ws_video);
        rd_stream_config_t scfg = {
            .fps_cap = s_cfg.fps_cap,
            .gop = s_cfg.h264_gop,
            .bitrate = s_cfg.h264_bitrate,
        };
        rd_stream_init(&scfg, s_server);
    }

    const esp_timer_create_args_t targs = {
        .callback = cursor_timer_cb,
        .name = "rd_cursor",
    };
    esp_timer_create(&targs, &s_cursor_timer);
    esp_timer_start_periodic(s_cursor_timer, RD_CURSOR_PERIOD_MS * 1000);

    char ip[16];
    wifi_get_ip_str(ip, sizeof(ip));
    FMRB_LOGI(TAG, "Remote desktop ready: http://%s/ (fps_cap=%u q=%u)",
              ip, s_cfg.fps_cap, s_cfg.jpeg_quality);
    return FMRB_OK;
}

void rd_http_disable_h264(void)
{
    if (s_cfg.h264_enable) {
        s_cfg.h264_enable = false;
        FMRB_LOGW(TAG, "H.264 disabled at runtime, viewers fall back to MJPEG");
    }
}

void rd_http_stop(void)
{
    atomic_store(&s_stop, true);
    // Wait for the detached MJPEG task to complete its async request
    // before tearing the server down
    while (atomic_load(&s_stream_busy)) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_cursor_timer) {
        esp_timer_stop(s_cursor_timer);
        esp_timer_delete(s_cursor_timer);
        s_cursor_timer = NULL;
    }
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    if (rd_encoder_jpeg_lock(2000) == FMRB_OK) {
        rd_encoder_jpeg_deinit();
        rd_encoder_jpeg_unlock();
    }
}
