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
#include "rd_http.h"

#include "fmrb_log.h"
#include "fmrb_rtos.h"
#include "display_p4_task.h"
#include "host_task.h"

#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include "fmrb_task_config.h"

static const char *TAG = "rd_stream";

#define RD_VIDEO_MAX_CLIENTS 2
// [0x01][flags][enc_w16][pts32][vis_w16][vis_h16][enc_h16], little endian.
// The encoded picture is padded to multiples of 16; the viewer shows the
// top-left vis_w x vis_h of it. The size can change from one frame to the
// next (the fullscreen high-resolution mode), always on an IDR.
#define RD_VIDEO_HDR_LEN     14

static rd_stream_config_t s_cfg;
static httpd_handle_t s_server = NULL;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static int s_fds[RD_VIDEO_MAX_CLIENTS];
static volatile bool s_task_running = false;
static volatile bool s_stop = false;

// Header + payload staging so one WS frame carries the whole access unit.
// Sized for the encoder's output budget, grown when the frame grows.
static uint8_t *s_pkt = NULL;
static size_t   s_pkt_cap = 0;

// The staging buffer for frames of w x h: the encoder's raw budget (its
// output never exceeds it) plus the header. Stream task (or before it runs).
static bool pkt_reserve(uint16_t w, uint16_t h)
{
    size_t need = RD_VIDEO_HDR_LEN
                + (size_t)((w + 15u) & ~15u) * ((h + 15u) & ~15u) * 2;
    if (s_pkt && s_pkt_cap >= need) return true;
    uint8_t *p = heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) {
        FMRB_LOGE(TAG, "pkt buffer alloc failed (%u bytes)", (unsigned)need);
        return false;
    }
    if (s_pkt) heap_caps_free(s_pkt);
    s_pkt = p;
    s_pkt_cap = need;
    return true;
}

// Bring the encoder up for frames of w x h, replacing one set up for another
// size. The first frame after it is an IDR (a fresh encoder starts with one).
static bool encoder_for(uint16_t w, uint16_t h)
{
    if (rd_encoder_h264_is_for(w, h)) return true;
    rd_encoder_h264_deinit();
    rd_h264_config_t ecfg = {
        .src_w = w, .src_h = h,
        .fps = s_cfg.fps_cap,
        .gop = s_cfg.gop,
        .bitrate = s_cfg.bitrate,
    };
    if (rd_encoder_h264_init(&ecfg) != FMRB_OK) {
        FMRB_LOGE(TAG, "H.264 encoder init failed for %ux%u", w, h);
        return false;
    }
    if (!pkt_reserve(w, h)) {
        rd_encoder_h264_deinit();
        return false;
    }
    rd_encoder_h264_request_idr();
    return true;
}

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

        // Wait one frame interval for new content, otherwise re-encode the
        // latest frame: browser H.264 decoders pipeline several frames and
        // only emit output as more input arrives, so a low-rate stream
        // (event-driven rendering idles at 1-2fps) shows seconds of latency.
        // A steady fps_cap stream keeps the decoder drained; unchanged
        // frames encode to near-empty P frames.
        display_p4_capture_frame_t frame;
        fmrb_err_t err = display_p4_capture_acquire(last_seq + 1,
                                                    frame_interval_ms, &frame);
        if (err == FMRB_ERR_TIMEOUT) {
            err = display_p4_capture_acquire(0, 100, &frame);
        }
        if (err != FMRB_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        last_seq = frame.seq;
        // The frame carries its own size: the fullscreen high-resolution mode
        // switches the display between 426x240 and 640x360 while the stream
        // runs. The encoder is set up for one size, so a new size brings it
        // up again (and the next frame is an IDR the viewer can resize on).
        if (!encoder_for(frame.width, frame.height)) {
            display_p4_capture_release();
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

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

        // Assemble the header + AU in one buffer
        size_t pkt_len = RD_VIDEO_HDR_LEN + au_len;
        if (pkt_len > s_pkt_cap) {
            // capacity fixed at init (raw frame size); encoder output
            // never exceeds it, but guard anyway
            FMRB_LOGW(TAG, "AU too large: %u", (unsigned)pkt_len);
            continue;
        }
        uint16_t w = rd_encoder_h264_width();
        uint16_t h = rd_encoder_h264_height();
        s_pkt[0] = 0x01;
        s_pkt[1] = is_idr ? 0x01 : 0x00;
        s_pkt[2] = (uint8_t)(w & 0xFF);
        s_pkt[3] = (uint8_t)(w >> 8);
        s_pkt[4] = (uint8_t)(pts_ms & 0xFF);
        s_pkt[5] = (uint8_t)((pts_ms >> 8) & 0xFF);
        s_pkt[6] = (uint8_t)((pts_ms >> 16) & 0xFF);
        s_pkt[7] = (uint8_t)((pts_ms >> 24) & 0xFF);
        s_pkt[8]  = (uint8_t)(frame.width & 0xFF);
        s_pkt[9]  = (uint8_t)(frame.width >> 8);
        s_pkt[10] = (uint8_t)(frame.height & 0xFF);
        s_pkt[11] = (uint8_t)(frame.height >> 8);
        s_pkt[12] = (uint8_t)(h & 0xFF);
        s_pkt[13] = (uint8_t)(h >> 8);
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

static bool ensure_task_running(void)
{
    if (s_task_running) return true;

    // Brought up here at the screen's current size, so that a broken encoder
    // is found while the client can still be told to fall back to MJPEG. The
    // stream task follows the frame size from then on.
    int sw = 0, sh = 0;
    fmrb_host_get_screen_size(&sw, &sh);
    if (sw <= 0 || sh <= 0) { sw = 426; sh = 240; }
    if (!encoder_for((uint16_t)sw, (uint16_t)sh)) return false;
    s_task_running = true;
    if (xTaskCreatePinnedToCore(stream_task, "rd_stream",
                                FMRB_RD_STREAM_TASK_STACK_SIZE, NULL,
                                FMRB_RD_STREAM_TASK_PRIORITY, NULL,
                                FMRB_RD_STREAM_TASK_CORE) != pdPASS) {
        FMRB_LOGE(TAG, "failed to create stream task");
        s_task_running = false;
        rd_encoder_h264_deinit();
        return false;
    }
    return true;
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
        httpd_sess_trigger_close(s_server, fd);
        return;
    }
    FMRB_LOGI(TAG, "video client fd=%d", fd);
    if (!ensure_task_running()) {
        // Encoder is unusable: release the slot, tell rd_http to stop
        // offering H.264 and close the socket so the viewer falls back
        rd_stream_remove_client(fd);
        rd_http_disable_h264();
        httpd_sess_trigger_close(s_server, fd);
        return;
    }
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
