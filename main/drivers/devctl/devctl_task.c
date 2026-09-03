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

#include "devctl_task.h"
#include "devctl_http.h"

#include "fmrb_log.h"
#include "fmrb_task_config.h"

#include "esp_http_server.h"

static const char *TAG = "devctl";

static httpd_handle_t s_server = NULL;

fmrb_err_t devctl_start(void)
{
    if (s_server) return FMRB_OK;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.core_id = FMRB_RD_HTTPD_TASK_CORE;
    cfg.task_priority = FMRB_RD_HTTPD_TASK_PRIORITY;
    cfg.stack_size = FMRB_RD_HTTPD_TASK_STACK_SIZE;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    // Eight routes, and the default is eight. Registration answers
    // ESP_ERR_HTTPD_HANDLERS_FULL rather than failing the start, so a server
    // sized exactly would come up healthy and 404 the last route added.
    cfg.max_uri_handlers = 12;

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
