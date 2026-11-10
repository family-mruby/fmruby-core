#pragma once

// What host_task.c lends the other host-task files (host_kanji.c). Host task
// only; not for other tasks.

#include <stddef.h>
#include <stdint.h>

#include "fmrb_err.h"
#include "fmrb_gfx_msg.h"
#include "fmrb_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

// Text to an app as one-byte key events, the way a composed kana goes.
void host_send_text_bytes(uint8_t pid, const char *bytes, size_t len);

// Graphics commands of the host task's own, straight to the display side in
// batches (they do not go through the host queue).
void host_send_gfx_list(const gfx_cmd_t *cmds, int count);

// A command that answers (CREATE_CANVAS): the callback runs in the host task
// when the answer arrives.
fmrb_err_t host_send_gfx_async(const gfx_cmd_t *cmd,
                               fmrb_transport_response_cb_t cb, void *user);

#ifdef __cplusplus
}
#endif
