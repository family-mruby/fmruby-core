// Per-VM debug context: the code_fetch_hook body, breakpoint/step logic, and
// the "park" mechanism (a stopped VM task services inspection commands from
// inside the hook, so mrb_state is only ever touched by its owning task).
//
// Threading model:
//   - The hook and the park loop run on the *VM's* FreeRTOS task.
//   - attach/detach/bp/pause/continue/step and the inspect calls run on the
//     *debugd* task and communicate with the parked VM via per-ctx queues.
// See doc/vm_remote_debug_design.md sec 5 and impl_plan.md sec 5.2.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "fmrb_err.h"
#include "fmrb_debug_proto.h"   // FMRB_DEBUG_MAX_* limits

// Step modes.
enum {
    FMRB_STEP_NONE = 0,
    FMRB_STEP_IN,
    FMRB_STEP_OVER,
    FMRB_STEP_OUT,
};

// Stop reasons (mirrors the protocol's stopped.reason strings).
enum {
    FMRB_STOP_BREAKPOINT = 0,
    FMRB_STOP_STEP,
    FMRB_STOP_PAUSE,
};

// Event delivered hook -> debugd (drained by fmrb_debug_ctx_poll_event).
typedef enum {
    FMRB_DBG_EV_STOPPED = 0,
    FMRB_DBG_EV_RESUMED,
    FMRB_DBG_EV_EXITED,
} fmrb_dbg_ev_type_t;

typedef struct {
    fmrb_dbg_ev_type_t type;
    int      pid;
    int      reason;                    // stopped: FMRB_STOP_*
    int      bp_id;                     // stopped/breakpoint: matched id, else -1
    int32_t  line;
    char     file[FMRB_DEBUG_MAX_FILE];
} fmrb_dbg_event_t;

// Initialize the ctx subsystem (event queue). Call once before attach.
fmrb_err_t fmrb_debug_ctx_init(void);

// --- called on the debugd task --------------------------------------------
fmrb_err_t fmrb_debug_ctx_attach(int pid);
fmrb_err_t fmrb_debug_ctx_detach(int pid);
bool       fmrb_debug_ctx_is_attached(int pid);
void       fmrb_debug_ctx_detach_all(void);

fmrb_err_t fmrb_debug_ctx_bp_set(int pid, const char *file, int line, int *out_bp_id);
fmrb_err_t fmrb_debug_ctx_bp_clear(int pid, int bp_id);   // bp_id < 0 clears all
fmrb_err_t fmrb_debug_ctx_pause(int pid);
fmrb_err_t fmrb_debug_ctx_continue(int pid);
fmrb_err_t fmrb_debug_ctx_step(int pid, int step_mode);

// Inspection: blocks up to a timeout while the parked VM task builds a msgpack
// payload (a map). On success *out_body/*out_len point into an internal buffer
// valid until the next inspect call (both run on the debugd task).
fmrb_err_t fmrb_debug_ctx_stack_trace(int pid, int max_frames,
                                      const uint8_t **out_body, size_t *out_len);
fmrb_err_t fmrb_debug_ctx_frame_vars(int pid, int frame,
                                     const uint8_t **out_body, size_t *out_len);

// Expand a value previously handed out by frame_vars/expand (its non-zero
// `ref`). Returns {"vars":[...]} for the container's children. Handles are
// only valid while the VM is parked at the current stop.
fmrb_err_t fmrb_debug_ctx_expand(int pid, int handle,
                                 const uint8_t **out_body, size_t *out_len);

// Drain one pending event (STOPPED/RESUMED/EXITED). Returns true if one was
// dequeued within timeout_ms.
bool fmrb_debug_ctx_poll_event(fmrb_dbg_event_t *out, uint32_t timeout_ms);
