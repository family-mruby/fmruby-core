// Debug helper for logging RProc details without header conflicts
// This file isolates mruby/proc.h and mruby/irep.h includes

#ifndef APP_DEBUG_H
#define APP_DEBUG_H

#include <mruby.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
struct RProc;

// Log detailed information about RProc object
// Logs flags, C function pointer or Ruby irep information
void app_debug_log_proc_details(
    mrb_state *mrb,
    const struct RProc *proc,
    const char *tag
);

// Dump irep bytecode, symbols, and constant pool
// Only valid for Ruby methods (CFUNC=0)
void app_debug_dump_irep_bytecode(
    mrb_state *mrb,
    const void *irep,  // const mrb_irep*
    const char *tag
);

// Dump entire call stack from cibase to ci
void app_debug_dump_callstack(
    mrb_state *mrb,
    const char *tag
);

#ifdef __cplusplus
}
#endif

#endif // APP_DEBUG_H
