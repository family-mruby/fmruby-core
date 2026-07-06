#pragma once

#include <stdint.h>
#include <stddef.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Remote-desktop input bridge: decodes the binary WebSocket input
// messages from the browser viewer and injects them through the same
// fmrb_host_send_* entry points the local input drivers use.
//
// Message format (little-endian), first byte = type:
//   0x01 mouse_move   { i16 x, i16 y }               (virtual 426x240)
//   0x02 mouse_button { i16 x, i16 y, u8 btn, u8 st } (btn: 1=L 2=M 3=R)
//   0x03 key          { u8 state, u8 hid_scancode, u8 fmrb_mod }
//   0x04 keyframe_req (Phase 2, handled by the stream layer)
//   0x05 ping         (no-op)

#define RD_INPUT_MSG_MOUSE_MOVE   0x01
#define RD_INPUT_MSG_MOUSE_BUTTON 0x02
#define RD_INPUT_MSG_KEY          0x03
#define RD_INPUT_MSG_KEYFRAME_REQ 0x04
#define RD_INPUT_MSG_PING         0x05

/**
 * @brief Decode and inject one input message.
 * @return FMRB_OK, or FMRB_ERR_INVALID_PARAM on a malformed message.
 */
fmrb_err_t rd_input_handle(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
