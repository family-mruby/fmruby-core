// The development control endpoints on a machine with no remote desktop.
//
// Modern registers them on the viewer's server (rd_http.c). Retro has no
// viewer -- the S3 has no hardware video encoder -- but it does have WiFi and
// it is the machine people write code on, so it gets the same launch/kill/list
// and file endpoints on a small server of its own.
//
// Started from boot.c only after WiFi has been asked for, so a machine with
// wifi_auto_start off never creates the task and pays nothing: the cost is one
// httpd task (8KB of stack) and its sockets.
//
// Four sockets rather than the viewer's seven. Nothing here streams, and the
// tools that use it (tools/fmrb_rd_fs.rb, curl) make one request at a time;
// four leaves room for a stuck connection to age out while another works.

#include <stdio.h>

#include "devctl_task.h"
#include "devctl_http.h"
#include "wifi_task.h"

#include "fmrb_log.h"
#include "fmrb_task_config.h"

#include "esp_http_server.h"

#if defined(FMRB_HAS_LOCAL_DISPLAY)
#include "display_local_task.h"
#endif

static const char *TAG = "devctl";

static httpd_handle_t s_server = NULL;

// The same small document the viewer's server answers (rd_http.c), so tools
// that check /status before anything else (tools/mcp) know this is a Family
// mruby board. There is no stream here: "mode" says so.
static esp_err_t status_handler(httpd_req_t *req)
{
    char ip[16];
    wifi_get_ip_str(ip, sizeof(ip));
    char body[112];
    // "fb": this board answers /fb (below), the one way to see its screen.
#if defined(FMRB_HAS_LOCAL_DISPLAY)
    const char *fb = ",\"fb\":true";
#else
    const char *fb = "";
#endif
    int n = snprintf(body, sizeof(body),
                     "{\"ip\":\"%s\",\"mode\":\"none\",\"streaming\":false%s}", ip, fb);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, n);
}

#if defined(FMRB_HAS_LOCAL_DISPLAY)
// One frame of the screen, as it is: RGB565 in the framebuffer's own byte
// order (little-endian 16-bit words), width x height, row by row. The size
// comes in X-Width / X-Height and the format in X-Format. For a board that
// composites its own screen but has no hardware encoder for the viewer's
// MJPEG stream (the AtomS3R: 128x128 is 32KB, small enough to send raw).
//
// It borrows the remote desktop's capture: enabled for this one request, a
// render asked for, the frame it produces copied out of the capture slot.
// The slots are external RAM, allocated by the first request and kept (two
// frames of the base screen; display_local_capture_enable). The send goes from
// the slot straight to the socket, so no internal RAM is used for it.
static esp_err_t fb_handler(httpd_req_t *req)
{
    fmrb_err_t err = display_local_capture_enable(true);
    if (err != FMRB_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"ok\":false,\"err\":\"no capture\"}",
                               HTTPD_RESP_USE_STRLEN);
    }
    display_local_capture_kick();
    display_local_capture_frame_t frame;
    err = display_local_capture_acquire(0, 2000, &frame);
    if (err != FMRB_OK) {
        display_local_capture_enable(false);
        httpd_resp_set_status(req, "504 Gateway Timeout");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"ok\":false,\"err\":\"no frame\"}",
                               HTTPD_RESP_USE_STRLEN);
    }
    char w[8], h[8];
    snprintf(w, sizeof(w), "%u", (unsigned)frame.width);
    snprintf(h, sizeof(h), "%u", (unsigned)frame.height);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "X-Width", w);
    httpd_resp_set_hdr(req, "X-Height", h);
    httpd_resp_set_hdr(req, "X-Format", "rgb565le");
    esp_err_t ret = httpd_resp_send(req, (const char *)frame.pixels,
                                    (ssize_t)frame.width * frame.height * 2);
    display_local_capture_release();
    display_local_capture_enable(false);
    return ret;
}
#endif

fmrb_err_t devctl_start(void)
{
    if (s_server) return FMRB_OK;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.core_id = FMRB_RD_HTTPD_TASK_CORE;
    cfg.task_priority = FMRB_RD_HTTPD_TASK_PRIORITY;
    cfg.stack_size = FMRB_RD_HTTPD_TASK_STACK_SIZE;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    // Thirteen routes, fourteen with /fb, and the default is eight.
    // Registration answers ESP_ERR_HTTPD_HANDLERS_FULL rather than failing the
    // start (the /fb registration below says so in the log).
    cfg.max_uri_handlers = 14;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        s_server = NULL;
        FMRB_LOGE(TAG, "httpd_start failed: %d", err);
        return FMRB_ERR_FAILED;
    }

    fmrb_err_t ferr = devctl_http_register(s_server);
    if (ferr != FMRB_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        return ferr;
    }

    static const httpd_uri_t uri_status = {
        .uri = "/status", .method = HTTP_GET, .handler = status_handler };
    err = httpd_register_uri_handler(s_server, &uri_status);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "could not register /status: %d", err);
    }

#if defined(FMRB_HAS_LOCAL_DISPLAY)
    static const httpd_uri_t uri_fb = {
        .uri = "/fb", .method = HTTP_GET, .handler = fb_handler };
    err = httpd_register_uri_handler(s_server, &uri_fb);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "could not register /fb: %d", err);
    }
#endif

    // devctl_http_register() announces the routes; it is the one that knows
    // they were actually registered.
    return FMRB_OK;
}

void devctl_stop(void)
{
    if (!s_server) return;
    httpd_stop(s_server);
    s_server = NULL;
}
