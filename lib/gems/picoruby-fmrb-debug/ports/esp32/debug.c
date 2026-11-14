// FMRB::Debug bindings: a thin wrapper over the on-device debug core
// (fmrb_debug_ctx) plus the session-owner arbitration (fmrb_debugd). See
// doc/editor_debug/design.md sec 4.1 (option B) and the E0/E1 plan.
//
// inspect payloads (stack_trace/frame_vars/expand) come back as raw msgpack
// bodies from a buffer that is only valid until the next inspect call, so each
// binding copies the body straight into an mrb_str; the mrblib wrapper decodes
// it with MessagePack.unpack. No msgpack decoder is needed in C.
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/array.h>

#include "fmrb_debug_ctx.h"
#include "fmrb_debugd.h"
#include "fmrb_app.h"
#include "fmrb_log.h"

#include "../../include/picoruby_fmrb_debug.h"

static const char *TAG = "dbg_gem";

// pid of the VM running this call (the debugger front-end itself).
static int self_pid(void) {
    fmrb_app_task_context_t *ctx = fmrb_current();
    return ctx ? (int)ctx->app_id : -1;
}

// Every hook-based call requires the local session. Guarding here keeps the
// core's single-consumer assumptions intact (the remote daemon skips the event
// queue and refuses hook commands while owner == LOCAL).
static bool require_local(void) {
    if (fmrb_debugd_owner() == FMRB_DBG_OWNER_LOCAL) return true;
    FMRB_LOGW(TAG, "FMRB::Debug hook call without local session (owner=%d)",
              (int)fmrb_debugd_owner());
    return false;
}

static mrb_value b(bool v) { return v ? mrb_true_value() : mrb_false_value(); }

// --- session ---------------------------------------------------------------

// FMRB::Debug.acquire -> bool (false = a remote client owns the session)
static mrb_value m_acquire(mrb_state *mrb, mrb_value self) {
    (void)self;
    return b(fmrb_debugd_acquire_local() == FMRB_OK);
}

// FMRB::Debug.release -> nil
static mrb_value m_release(mrb_state *mrb, mrb_value self) {
    (void)self;
    fmrb_debugd_release_local();
    return mrb_nil_value();
}

// FMRB::Debug.source_file(pid) -> String path or nil. Returns the target's
// script path (set at spawn for file-loaded apps), so a front-end can open the
// source on attach -- before any stop -- to place breakpoints. Reads a static
// context field; no VM interaction, safe while the target runs.
static mrb_value m_source_file(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_int pid;
    mrb_get_args(mrb, "i", &pid);
    fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id((int)pid);
    if (!ctx || ctx->filepath[0] == '\0') return mrb_nil_value();
    return mrb_str_new_cstr(mrb, ctx->filepath);
}

// FMRB::Debug.owner -> :none / :remote / :local
static mrb_value m_owner(mrb_state *mrb, mrb_value self) {
    (void)self;
    const char *name;
    switch (fmrb_debugd_owner()) {
        case FMRB_DBG_OWNER_REMOTE: name = "remote"; break;
        case FMRB_DBG_OWNER_LOCAL:  name = "local";  break;
        default:                    name = "none";   break;
    }
    return mrb_symbol_value(mrb_intern_cstr(mrb, name));
}

// --- attach / breakpoints / flow -------------------------------------------

// FMRB::Debug.attach(pid) -> bool
static mrb_value m_attach(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_int pid;
    mrb_get_args(mrb, "i", &pid);
    if (!require_local()) return mrb_false_value();
    // Attaching to our own VM would deadlock: inspect calls park the target and
    // block the caller waiting on it -- here they are the same task.
    if ((int)pid == self_pid()) {
        FMRB_LOGW(TAG, "refusing self-attach (pid=%d)", (int)pid);
        return mrb_false_value();
    }
    return b(fmrb_debug_ctx_attach((int)pid) == FMRB_OK);
}

// FMRB::Debug.detach(pid) -> bool
static mrb_value m_detach(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_int pid;
    mrb_get_args(mrb, "i", &pid);
    if (!require_local()) return mrb_false_value();
    return b(fmrb_debug_ctx_detach((int)pid) == FMRB_OK);
}

// FMRB::Debug.attached?(pid) -> bool
static mrb_value m_attached_p(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_int pid;
    mrb_get_args(mrb, "i", &pid);
    return b(fmrb_debug_ctx_is_attached((int)pid));
}

// FMRB::Debug.bp_set(pid, file, line) -> Integer bp_id or nil
static mrb_value m_bp_set(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_int pid, line;
    char *file;
    mrb_get_args(mrb, "izi", &pid, &file, &line);
    if (!require_local()) return mrb_nil_value();
    int bp_id = -1;
    if (fmrb_debug_ctx_bp_set((int)pid, file, (int)line, &bp_id) != FMRB_OK) {
        return mrb_nil_value();
    }
    return mrb_fixnum_value(bp_id);
}

// FMRB::Debug.bp_clear(pid, bp_id = -1) -> bool  (bp_id < 0 clears all)
static mrb_value m_bp_clear(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_int pid;
    mrb_int bp_id = -1;
    mrb_get_args(mrb, "i|i", &pid, &bp_id);
    if (!require_local()) return mrb_false_value();
    return b(fmrb_debug_ctx_bp_clear((int)pid, (int)bp_id) == FMRB_OK);
}

// pause / continue / step_* all take (pid) and return bool.
static mrb_value m_pause(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_int pid; mrb_get_args(mrb, "i", &pid);
    if (!require_local()) return mrb_false_value();
    return b(fmrb_debug_ctx_pause((int)pid) == FMRB_OK);
}

static mrb_value m_continue(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_int pid; mrb_get_args(mrb, "i", &pid);
    if (!require_local()) return mrb_false_value();
    return b(fmrb_debug_ctx_continue((int)pid) == FMRB_OK);
}

static mrb_value step_common(mrb_state *mrb, int mode) {
    mrb_int pid; mrb_get_args(mrb, "i", &pid);
    if (!require_local()) return mrb_false_value();
    return b(fmrb_debug_ctx_step((int)pid, mode) == FMRB_OK);
}
static mrb_value m_step_in(mrb_state *mrb, mrb_value self)   { (void)self; return step_common(mrb, FMRB_STEP_IN); }
static mrb_value m_step_over(mrb_state *mrb, mrb_value self) { (void)self; return step_common(mrb, FMRB_STEP_OVER); }
static mrb_value m_step_out(mrb_state *mrb, mrb_value self)  { (void)self; return step_common(mrb, FMRB_STEP_OUT); }

// --- inspect (raw msgpack bodies) ------------------------------------------

// Copy the inspect body into a fresh mrb_str (the core buffer is only valid
// until the next inspect call), or nil on error.
static mrb_value inspect_to_str(mrb_state *mrb, fmrb_err_t err,
                                const uint8_t *body, size_t len) {
    if (err != FMRB_OK || !body) return mrb_nil_value();
    return mrb_str_new(mrb, (const char *)body, len);
}

// FMRB::Debug._stack_trace_raw(pid, max) -> String (msgpack) or nil
static mrb_value m_stack_trace_raw(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_int pid, max;
    mrb_get_args(mrb, "ii", &pid, &max);
    if (!require_local()) return mrb_nil_value();
    const uint8_t *body = NULL; size_t len = 0;
    fmrb_err_t err = fmrb_debug_ctx_stack_trace((int)pid, (int)max, &body, &len);
    return inspect_to_str(mrb, err, body, len);
}

// FMRB::Debug._frame_vars_raw(pid, frame) -> String (msgpack) or nil
static mrb_value m_frame_vars_raw(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_int pid, frame;
    mrb_get_args(mrb, "ii", &pid, &frame);
    if (!require_local()) return mrb_nil_value();
    const uint8_t *body = NULL; size_t len = 0;
    fmrb_err_t err = fmrb_debug_ctx_frame_vars((int)pid, (int)frame, &body, &len);
    return inspect_to_str(mrb, err, body, len);
}

// FMRB::Debug._expand_raw(pid, ref) -> String (msgpack) or nil
static mrb_value m_expand_raw(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_int pid, ref;
    mrb_get_args(mrb, "ii", &pid, &ref);
    if (!require_local()) return mrb_nil_value();
    const uint8_t *body = NULL; size_t len = 0;
    fmrb_err_t err = fmrb_debug_ctx_expand((int)pid, (int)ref, &body, &len);
    return inspect_to_str(mrb, err, body, len);
}

// --- events ----------------------------------------------------------------

// FMRB::Debug._poll_event_raw(timeout_ms) -> [type, pid, reason, bp_id, line,
// file] or nil. Fixed array (no msgpack): the mrblib wrapper shapes the Hash.
static mrb_value m_poll_event_raw(mrb_state *mrb, mrb_value self) {
    (void)self;
    mrb_int timeout_ms;
    mrb_get_args(mrb, "i", &timeout_ms);
    if (!require_local()) return mrb_nil_value();
    fmrb_dbg_event_t ev;
    if (!fmrb_debug_ctx_poll_event(&ev, (uint32_t)timeout_ms)) return mrb_nil_value();
    mrb_value a = mrb_ary_new_capa(mrb, 6);
    mrb_ary_push(mrb, a, mrb_fixnum_value((int)ev.type));
    mrb_ary_push(mrb, a, mrb_fixnum_value(ev.pid));
    mrb_ary_push(mrb, a, mrb_fixnum_value(ev.reason));
    mrb_ary_push(mrb, a, mrb_fixnum_value(ev.bp_id));
    mrb_ary_push(mrb, a, mrb_fixnum_value(ev.line));
    mrb_ary_push(mrb, a, mrb_str_new_cstr(mrb, ev.file));
    return a;
}

// --- registration ----------------------------------------------------------

void mrb_picoruby_fmrb_debug_init_impl(mrb_state *mrb) {
    struct RClass *fmrb = mrb_define_module(mrb, "FMRB");
    struct RClass *dbg  = mrb_define_module_under(mrb, fmrb, "Debug");

    mrb_define_module_function(mrb, dbg, "acquire", m_acquire, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, dbg, "release", m_release, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, dbg, "owner",   m_owner,   MRB_ARGS_NONE());
    mrb_define_module_function(mrb, dbg, "source_file", m_source_file, MRB_ARGS_REQ(1));

    mrb_define_module_function(mrb, dbg, "attach",    m_attach,     MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, dbg, "detach",    m_detach,     MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, dbg, "attached?", m_attached_p, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, dbg, "bp_set",    m_bp_set,     MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, dbg, "bp_clear",  m_bp_clear,   MRB_ARGS_ARG(1, 1));

    mrb_define_module_function(mrb, dbg, "pause",     m_pause,     MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, dbg, "continue",  m_continue,  MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, dbg, "step_in",   m_step_in,   MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, dbg, "step_over", m_step_over, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, dbg, "step_out",  m_step_out,  MRB_ARGS_REQ(1));

    mrb_define_module_function(mrb, dbg, "_stack_trace_raw", m_stack_trace_raw, MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, dbg, "_frame_vars_raw",  m_frame_vars_raw,  MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, dbg, "_expand_raw",      m_expand_raw,      MRB_ARGS_REQ(2));
    mrb_define_module_function(mrb, dbg, "_poll_event_raw",  m_poll_event_raw,  MRB_ARGS_REQ(1));

    FMRB_LOGI(TAG, "FMRB::Debug registered");
}

void mrb_picoruby_fmrb_debug_final_impl(mrb_state *mrb) {
    (void)mrb;
}
