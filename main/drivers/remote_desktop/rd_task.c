// Remote-desktop lifecycle (Modern only).
//
// rd_task_init reads the [remote_desktop] config, then spawns a one-shot
// task that waits until WiFi has an IP and starts the HTTP server. Kept
// asynchronous so boot never blocks on network availability.

#include "rd_task.h"
#include "rd_http.h"

#include "fmrb_log.h"
#include "fmrb_toml.h"
#include "fmrb_rtos.h"
#include "wifi_task.h"

#include <string.h>
#include "fmrb_task_config.h"

static const char *TAG = "rd_task";

#define RD_CONF_PATH "/etc/system_conf.toml"

static rd_http_config_t s_http_cfg = {
    .fps_cap = 15,
    .jpeg_quality = 80,
    .h264_enable = true,
    .h264_gop = 30,
    .h264_bitrate = 1000000,
};

static void rd_start_task(void *arg)
{
    (void)arg;
    // WiFi reconnects forever in the background; just wait generously.
    while (!wifi_wait_for_ip(60000)) {
        FMRB_LOGI(TAG, "still waiting for WiFi...");
    }
    if (rd_http_start(&s_http_cfg) != FMRB_OK) {
        FMRB_LOGE(TAG, "failed to start remote desktop server");
    }
    fmrb_task_delete_ex(NULL);
}

fmrb_err_t rd_task_init(void)
{
    char errbuf[128];
    toml_table_t *root = fmrb_toml_load_file(RD_CONF_PATH, errbuf, sizeof(errbuf));
    if (root) {
        toml_table_t *rd = toml_table_in(root, "remote_desktop");
        if (rd) {
            if (!fmrb_toml_get_bool(rd, "enable", true)) {
                toml_free(root);
                FMRB_LOGI(TAG, "remote desktop disabled in config");
                return FMRB_ERR_NOT_FOUND;
            }
            int64_t fps = fmrb_toml_get_int(rd, "fps_cap", 15);
            int64_t q   = fmrb_toml_get_int(rd, "jpeg_quality", 80);
            int64_t br  = fmrb_toml_get_int(rd, "h264_bitrate_kbps", 1000);
            int64_t gop = fmrb_toml_get_int(rd, "h264_gop", 30);
            const char *mode = fmrb_toml_get_string(rd, "mode", "h264");
            if (fps < 1) fps = 1;
            if (fps > 30) fps = 30;
            if (q < 10) q = 10;
            if (q > 100) q = 100;
            if (br < 100) br = 100;
            if (br > 10000) br = 10000;
            if (gop < 1) gop = 1;
            if (gop > 120) gop = 120;
            s_http_cfg.fps_cap = (uint8_t)fps;
            s_http_cfg.jpeg_quality = (uint8_t)q;
            s_http_cfg.h264_enable = (strcmp(mode, "mjpeg") != 0);
            s_http_cfg.h264_gop = (uint8_t)gop;
            s_http_cfg.h264_bitrate = (uint32_t)(br * 1000);
        }
        toml_free(root);
    }

    if (fmrb_task_create(rd_start_task, "rd_start", FMRB_RD_START_TASK_STACK_SIZE,
                         NULL, FMRB_RD_START_TASK_PRIORITY, NULL)
        != FMRB_PASS) {
        FMRB_LOGE(TAG, "failed to spawn rd_start task");
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}
