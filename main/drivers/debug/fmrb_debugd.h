// Remote debugger daemon (doc/vm_remote_debug_*). Owns the debug session:
// a single FreeRTOS task that runs the transport, decodes requests, and
// dispatches them. Linux/Phase 1 only for now.
#pragma once

#include "fmrb_err.h"

// Start the debugd task. Safe to call once, after fmrb_app / fmrb_msg are up
// (boot.c, after fmrb_kernel_start). No-op if already started.
void fmrb_debugd_init(void);

// --- session owner (Phase E0) ---------------------------------------------
// The debug core (fmrb_debug_ctx) is single-client: its inspect buffer and
// event queue assume one consumer. Two front-ends can drive it -- the remote
// transport (this daemon) and the on-device FMRB::Debug gem -- so exactly one
// owner may hold it at a time.
typedef enum {
    FMRB_DBG_OWNER_NONE = 0,
    FMRB_DBG_OWNER_REMOTE,   // TCP/BLE client (this daemon's dispatch path)
    FMRB_DBG_OWNER_LOCAL,    // on-device FMRB::Debug gem
} fmrb_dbg_owner_t;

// Acquire the local (on-device) session. Transitions NONE -> LOCAL. Returns
// FMRB_OK if the local owner now holds it (already-LOCAL is idempotent),
// FMRB_ERR_BUSY if a remote client owns it. Called from a VM task (the gem).
fmrb_err_t fmrb_debugd_acquire_local(void);

// Release the local session (LOCAL -> NONE). Detaches every VM first so the
// gem dropping the session never leaves a VM parked. No-op if not local-owned.
void fmrb_debugd_release_local(void);

// Current session owner (acquire-loaded). Safe from any task.
fmrb_dbg_owner_t fmrb_debugd_owner(void);
