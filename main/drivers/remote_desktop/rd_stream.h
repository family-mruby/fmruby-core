#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "fmrb_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// H.264 WebSocket streamer (Phase 2): one encode, fan-out to all
// /ws_video clients. Frame format (little-endian):
//   [u8 type=0x01][u8 flags(bit0=keyframe)][u16 width][u32 pts_ms][Annex B AU]

typedef struct {
    uint8_t  fps_cap;
    uint8_t  gop;
    uint32_t bitrate;   // bits per second
} rd_stream_config_t;

fmrb_err_t rd_stream_init(const rd_stream_config_t *cfg, httpd_handle_t server);
void       rd_stream_stop(void);

/** Register a newly connected /ws_video client (requests an IDR). */
void rd_stream_add_client(int fd);
void rd_stream_remove_client(int fd);

/** Force an IDR on the next encoded frame. */
void rd_stream_request_idr(void);

/** True when at least one video client is connected. */
bool rd_stream_has_clients(void);

#ifdef __cplusplus
}
#endif
