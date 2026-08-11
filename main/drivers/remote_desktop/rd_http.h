#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Remote-desktop HTTP/WebSocket server (esp_http_server).
// URIs: "/" + viewer assets (embedded), "/stream" (MJPEG multipart),
// "/ws" (binary input + JSON cursor/status), "/status" (JSON).

typedef struct {
    uint8_t  fps_cap;        // max streamed frames per second
    uint8_t  jpeg_quality;   // 1..100
    bool     h264_enable;    // offer the /ws_video H.264 stream (Phase 2)
    uint8_t  h264_gop;       // keyframe interval in frames
    uint32_t h264_bitrate;   // bits per second
} rd_http_config_t;

fmrb_err_t rd_http_start(const rd_http_config_t *cfg);
void       rd_http_stop(void);

/**
 * @brief Stop offering H.264 to new clients (encoder failed at runtime).
 * Subsequent /ws info messages report h264:false so viewers pick MJPEG.
 */
void rd_http_disable_h264(void);

/**
 * @brief What is being streamed right now, for the desktop status icon.
 * @return 0 = nothing, 1 = MJPEG stream running, 2 = H.264 clients connected.
 *
 * Bare atomic reads; safe to poll from an app task at 1Hz.
 */
int rd_http_stream_state(void);

#ifdef __cplusplus
}
#endif
