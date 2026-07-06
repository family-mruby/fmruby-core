// H.264 WebSocket streamer (remote desktop Phase 2).
//
// A dedicated task on core 1 (below the display task priority) captures
// frames, encodes them once with the P4 hardware H.264 encoder and fans
// the Annex B access units out to every /ws_video client. The task runs
// only while clients are connected; the encoder and the display capture
// are brought up lazily with the first client.
//
// httpd_ws_send_frame_async is called directly from this task: each
// /ws_video socket is written only by this task (the httpd task only
// reads from it), so no send interleaving can occur.

#include "rd_stream.h"
#include "rd_encoder_h264.h"

#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include "display_p4_task.h"

#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "rd_stream";

#define RD_VIDEO_MAX_CLIENTS 2
#define RD_VIDEO_HDR_LEN     8

static rd_stream_config_t s_cfg;
static httpd_handle_t s_server = NULL;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static int s_fds[RD_VIDEO_MAX_CLIENTS];
static volatile bool s_task_running = false;
static volatile bool s_stop = false;

// Header + payload staging so one WS frame carries the whole access unit
static uint8_t *s_pkt = NULL;
static size_t   s_pkt_cap = 0;

bool rd_stream_has_clients(void)
{
    bool any = false;
    portENTER_CRITICAL(&s_lock);
    for (int i = 0; i < RD_VIDEO_MAX_CLIENTS; i++) {
        if (s_fds[i] >= 0) { any = true; break; }
    }
    portEXIT_CRITICAL(&s_lock);
    return any;
}

void rd_stream_request_idr(void)
{
    rd_encoder_h264_request_idr();
}

static void stream_task(void *arg)
{
    (void)arg;
    FMRB_LOGI(TAG, "video stream task started");

    display_p4_capture_enable(true);
    display_p4_capture_kick();
    rd_encoder_h264_request_idr();

    const uint32_t frame_interval_ms =
        (s_cfg.fps_cap > 0) ? (1000u / s_cfg.fps_cap) : 66u;
    uint32_t last_seq = 0;
    int64_t t0 = esp_timer_get_time();

    while (!s_stop && rd_stream_has_clients()) {
        int64_t t_frame = esp_timer_get_time();

        display_p4_capture_frame_t frame;
        fmrb_err_t err = display_p4_capture_acquire(last_seq + 1, 500, &frame);
        if (err == FMRB_ERR_TIMEOUT) {
            // Static screen: resend the latest frame as keepalive
            err = display_p4_capture_acquire(0, 100, &frame);
        }
        if (err != FMRB_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        last_seq = frame.seq;

        uint32_t pts_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        const uint8_t *au = NULL;
        size_t au_len = 0;
        bool is_idr = false;
        err = rd_encoder_h264_encode(frame.pixels, pts_ms, &au, &au_len, &is_idr);
        display_p4_capture_release();
        if (err != FMRB_OK) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // Assemble [type][flags][width][pts] + AU in one buffer
        size_t pkt_len = RD_VIDEO_HDR_LEN + au_len;
        if (pkt_len > s_pkt_cap) {
            // capacity fixed at init (raw frame size); encoder output
            // never exceeds it, but guard anyway
            FMRB_LOGW(TAG, "AU too large: %u", (unsigned)pkt_len);
            continue;
        }
        uint16_t w = rd_encoder_h264_width();
        s_pkt[0] = 0x01;
        s_pkt[1] = is_idr ? 0x01 : 0x00;
        s_pkt[2] = (uint8_t)(w & 0xFF);
        s_pkt[3] = (uint8_t)(w >> 8);
        s_pkt[4] = (uint8_t)(pts_ms & 0xFF);
        s_pkt[5] = (uint8_t)((pts_ms >> 8) & 0xFF);
        s_pkt[6] = (uint8_t)((pts_ms >> 16) & 0xFF);
        s_pkt[7] = (uint8_t)((pts_ms >> 24) & 0xFF);
        memcpy(s_pkt + RD_VIDEO_HDR_LEN, au, au_len);

        httpd_ws_frame_t f = {
            .type = HTTPD_WS_TYPE_BINARY,
            .payload = s_pkt,
            .len = pkt_len,
        };
        int fds[RD_VIDEO_MAX_CLIENTS];
        portENTER_CRITICAL(&s_lock);
        memcpy(fds, s_fds, sizeof(fds));
        portEXIT_CRITICAL(&s_lock);
        for (int i = 0; i < RD_VIDEO_MAX_CLIENTS; i++) {
            if (fds[i] < 0) continue;
            if (httpd_ws_send_frame_async(s_server, fds[i], &f) != ESP_OK) {
                rd_stream_remove_client(fds[i]);
            }
        }

        int64_t now = esp_timer_get_time();
        uint32_t spent_ms = (uint32_t)((now - t_frame) / 1000);
        if (spent_ms < frame_interval_ms) {
            vTaskDelay(pdMS_TO_TICKS(frame_interval_ms - spent_ms));
        }
    }

    display_p4_capture_enable(false);
    rd_encoder_h264_deinit();
    FMRB_LOGI(TAG, "video stream task stopped");
    s_task_running = false;
    fmrb_task_delete_ex(NULL);
}

static void ensure_task_running(void)
{
    if (s_task_running) return;

    rd_h264_config_t ecfg = {
        .src_w = 426, .src_h = 240,
        .fps = s_cfg.fps_cap,
        .gop = s_cfg.gop,
        .bitrate = s_cfg.bitrate,
    };
    if (rd_encoder_h264_init(&ecfg) != FMRB_OK) {
        FMRB_LOGE(TAG, "H.264 encoder init failed");
        return;
    }
    if (!s_pkt) {
        s_pkt_cap = RD_VIDEO_HDR_LEN + (size_t)432 * 240 * 2;
        s_pkt = heap_caps_malloc(s_pkt_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_pkt) {
            FMRB_LOGE(TAG, "pkt buffer alloc failed");
            return;
        }
    }
    s_task_running = true;
    if (xTaskCreatePinnedToCore(stream_task, "rd_stream", 8192, NULL, 4,
                                NULL, 1) != pdPASS) {
        FMRB_LOGE(TAG, "failed to create stream task");
        s_task_running = false;
    }
}

void rd_stream_add_client(int fd)
{
    portENTER_CRITICAL(&s_lock);
    bool added = false;
    for (int i = 0; i < RD_VIDEO_MAX_CLIENTS && !added; i++) {
        if (s_fds[i] == fd) added = true;
    }
    for (int i = 0; i < RD_VIDEO_MAX_CLIENTS && !added; i++) {
        if (s_fds[i] < 0) { s_fds[i] = fd; added = true; }
    }
    portEXIT_CRITICAL(&s_lock);
    if (!added) {
        FMRB_LOGW(TAG, "video client limit reached, fd=%d rejected", fd);
        return;
    }
    FMRB_LOGI(TAG, "video client fd=%d", fd);
    ensure_task_running();
    rd_encoder_h264_request_idr();
}

void rd_stream_remove_client(int fd)
{
    portENTER_CRITICAL(&s_lock);
    for (int i = 0; i < RD_VIDEO_MAX_CLIENTS; i++) {
        if (s_fds[i] == fd) s_fds[i] = -1;
    }
    portEXIT_CRITICAL(&s_lock);
}

fmrb_err_t rd_stream_init(const rd_stream_config_t *cfg, httpd_handle_t server)
{
    if (!cfg || !server) return FMRB_ERR_INVALID_PARAM;
    s_cfg = *cfg;
    s_server = server;
    s_stop = false;
    for (int i = 0; i < RD_VIDEO_MAX_CLIENTS; i++) s_fds[i] = -1;
    return FMRB_OK;
}

void rd_stream_stop(void)
{
    s_stop = true;
    portENTER_CRITICAL(&s_lock);
    for (int i = 0; i < RD_VIDEO_MAX_CLIENTS; i++) s_fds[i] = -1;
    portEXIT_CRITICAL(&s_lock);
    while (s_task_running) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_pkt) { heap_caps_free(s_pkt); s_pkt = NULL; s_pkt_cap = 0; }
}
