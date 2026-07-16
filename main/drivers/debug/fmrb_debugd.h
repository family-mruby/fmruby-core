// Remote debugger daemon (doc/vm_remote_debug_*). Owns the debug session:
// a single FreeRTOS task that runs the transport, decodes requests, and
// dispatches them. Linux/Phase 1 only for now.
#pragma once

// Start the debugd task. Safe to call once, after fmrb_app / fmrb_msg are up
// (boot.c, after fmrb_kernel_start). No-op if already started.
void fmrb_debugd_init(void);
