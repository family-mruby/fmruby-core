// Per-VM debug context + code_fetch_hook + park loop. See fmrb_debug_ctx.h.
#include "fmrb_debug_ctx.h"

#include <string.h>

#include <picoruby.h>
#include <mruby/irep.h>
#include <mruby/debug.h>
#include <mruby/proc.h>
#include <mruby/string.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/class.h>

#include "fmrb_app.h"
#include "fmrb_log.h"
#include "fmrb_rtos.h"

static const char *TAG = "dbg_ctx";

#define PARK_BUF_SIZE   4096     // per-ctx msgpack scratch for inspect payloads
                                 // (<= FMRB_DEBUG_MAX_FRAME transport limit)
#define EVENT_Q_DEPTH   8
#define MAX_HANDLES     128      // expandable-value handles live per stop
#define MAX_CHILDREN    20       // children packed per expand (rest summarized)

// --- park command / response (debugd <-> parked VM task) -------------------
typedef enum {
    PARK_CONTINUE = 0,
    PARK_STEP_IN,
    PARK_STEP_OVER,
    PARK_STEP_OUT,
    PARK_STACK_TRACE,
    PARK_FRAME_VARS,
    PARK_EXPAND,
    PARK_DETACH,
} park_op_t;

typedef struct {
    park_op_t op;
    int       arg;      // stack_trace: max frames; frame_vars: frame index
} park_cmd_t;

typedef struct {
    int             err;
    const uint8_t  *body;   // points into ctx->park_buf
    size_t          len;
} park_resp_t;

// --- breakpoint ------------------------------------------------------------
typedef struct {
    int   id;
    int   line;
    bool  enabled;
    char  file[FMRB_DEBUG_MAX_FILE];
} fmrb_dbg_bp_t;

// --- per-VM context --------------------------------------------------------
typedef struct {
    bool          in_use;
    int           pid;
    mrb_state    *mrb;
    uint32_t      gen;

    // hook-visible control (VM task reads, debugd writes)
    volatile bool armed;
    volatile bool pause_req;
    volatile int  step_mode;
    const mrb_callinfo    *step_ci;
    const struct mrb_context *step_c;

    fmrb_dbg_bp_t bps[FMRB_DEBUG_MAX_BP];
    int           next_bp_id;

    // stop state
    volatile bool  stopped;
    const mrb_irep *stop_irep;
    int32_t        stop_line;
    bool           at_stop;      // sitting on the line we last stopped at
    mrb_value     *cur_regs;     // top-frame registers at park time

    // expandable-value handles (variablesReference). Reset every stop; valid
    // only while parked (no GC runs, so the stored values stay reachable).
    mrb_value      handles[MAX_HANDLES];
    int            nhandles;

    // park comms
    fmrb_queue_t  cmd_q;         // debugd -> parked VM
    fmrb_queue_t  resp_q;        // parked VM -> debugd
    uint8_t       park_buf[PARK_BUF_SIZE];
} fmrb_debug_ctx_t;

static fmrb_debug_ctx_t s_dctx[FMRB_DEBUG_MAX_ATTACH];
static fmrb_queue_t     s_event_q;   // hook -> debugd

// ==========================================================================
// small helpers
// ==========================================================================
static const char *base_name(const char *p) {
    if (!p) return "";
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static bool file_match(const char *a, const char *b) {
    return strcmp(base_name(a), base_name(b)) == 0;
}

// reverse lookup mrb -> ctx (hook fast path). At most FMRB_DEBUG_MAX_ATTACH.
static fmrb_debug_ctx_t *ctx_by_mrb(mrb_state *mrb) {
    for (int i = 0; i < FMRB_DEBUG_MAX_ATTACH; i++) {
        if (s_dctx[i].in_use && s_dctx[i].mrb == mrb) return &s_dctx[i];
    }
    return NULL;
}

static fmrb_debug_ctx_t *ctx_by_pid(int pid) {
    for (int i = 0; i < FMRB_DEBUG_MAX_ATTACH; i++) {
        if (s_dctx[i].in_use && s_dctx[i].pid == pid) return &s_dctx[i];
    }
    return NULL;
}

static void emit_event(fmrb_dbg_ev_type_t type, int pid, int reason,
                       int bp_id, int32_t line, const char *file) {
    fmrb_dbg_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.pid = pid;
    ev.reason = reason;
    ev.bp_id = bp_id;
    ev.line = line;
    if (file) {
        strncpy(ev.file, file, sizeof(ev.file) - 1);
        ev.file[sizeof(ev.file) - 1] = '\0';
    }
    fmrb_queue_send(s_event_q, &ev, 0);   // non-blocking; drop if full
}

static void ctx_free(fmrb_debug_ctx_t *d) {
    if (d->cmd_q)  { fmrb_queue_delete(d->cmd_q);  d->cmd_q = NULL; }
    if (d->resp_q) { fmrb_queue_delete(d->resp_q); d->resp_q = NULL; }
    memset(d, 0, sizeof(*d));
}

// Re-fetch the app by pid and confirm gen/mrb still match. On mismatch the app
// exited or its slot was reused: force-free the ctx and emit `exited`.
static fmrb_debug_ctx_t *ctx_valid(int pid) {
    fmrb_debug_ctx_t *d = ctx_by_pid(pid);
    if (!d) return NULL;
    fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id(pid);
    if (!ctx || ctx->gen != d->gen || ctx->mrb != d->mrb) {
        FMRB_LOGW(TAG, "pid %d vanished/reused; freeing dctx", pid);
        emit_event(FMRB_DBG_EV_EXITED, pid, 0, -1, 0, NULL);
        ctx_free(d);
        return NULL;
    }
    return d;
}

// ==========================================================================
// line / breakpoint helpers (run in hook, VM task)
// ==========================================================================
static bool is_line_start(mrb_state *mrb, const mrb_irep *irep,
                          uint32_t pc_off, int32_t line) {
    if (pc_off == 0) return true;
    return mrb_debug_get_line(mrb, irep, pc_off - 1) != line;
}

static int bp_match(fmrb_debug_ctx_t *d, const char *file, int32_t line) {
    for (int i = 0; i < FMRB_DEBUG_MAX_BP; i++) {
        if (d->bps[i].enabled && d->bps[i].line == line &&
            file_match(file, d->bps[i].file)) {
            return d->bps[i].id;
        }
    }
    return -1;
}

// ==========================================================================
// fixed-buffer msgpack sink (no malloc inside the park loop)
// ==========================================================================
typedef struct { uint8_t *buf; size_t cap; size_t len; bool overflow; } fixbuf_t;

static int fixbuf_write(void *data, const char *b, size_t l) {
    fixbuf_t *f = (fixbuf_t *)data;
    if (f->len + l > f->cap) { f->overflow = true; return -1; }
    memcpy(f->buf + f->len, b, l);
    f->len += l;
    return 0;
}

// ==========================================================================
// stack walking (run in park loop, VM task, self context)
// ==========================================================================
// Resolve a Ruby callinfo for frame index (0 = top). Returns NULL if the frame
// is out of range or not a Ruby (irep-backed) frame.
static const mrb_callinfo *frame_ci(mrb_state *mrb, int frame, bool *is_top) {
    struct mrb_context *c = mrb->c;
    ptrdiff_t top = c->ci - c->cibase;
    ptrdiff_t idx = top - frame;
    if (idx < 0 || idx > top) return NULL;
    if (is_top) *is_top = (frame == 0);
    return &c->cibase[idx];
}

static const mrb_irep *ci_irep(const mrb_callinfo *ci) {
    if (ci->proc && !MRB_PROC_CFUNC_P(ci->proc)) return ci->proc->body.irep;
    return NULL;
}

static int32_t ci_line(mrb_state *mrb, const mrb_callinfo *ci,
                       const mrb_irep *irep, bool is_top) {
    if (!irep || !irep->debug_info || !ci->pc) return -1;
    const mrb_code *pc = is_top ? ci->pc : ci->pc - 1;
    if (pc < irep->iseq) return -1;
    return mrb_debug_get_line(mrb, irep, (uint32_t)(pc - irep->iseq));
}

static const char *ci_file(mrb_state *mrb, const mrb_callinfo *ci,
                           const mrb_irep *irep, bool is_top) {
    if (!irep || !irep->debug_info || !ci->pc) return NULL;
    const mrb_code *pc = is_top ? ci->pc : ci->pc - 1;
    if (pc < irep->iseq) return NULL;
    return mrb_debug_get_filename(mrb, irep, (uint32_t)(pc - irep->iseq));
}

// Build {"frames":[{idx,func,file,line}...]} into d->park_buf.
static park_resp_t build_stack_trace(fmrb_debug_ctx_t *d, int max_frames) {
    mrb_state *mrb = d->mrb;
    fixbuf_t fb = { d->park_buf, sizeof(d->park_buf), 0, false };
    msgpack_packer pk;
    msgpack_packer_init(&pk, &fb, fixbuf_write);

    // Count Ruby frames first (cap by max_frames).
    struct mrb_context *c = mrb->c;
    ptrdiff_t top = c->ci - c->cibase;
    int count = 0;
    for (int f = 0; f <= top && (max_frames <= 0 || count < max_frames); f++) {
        bool is_top;
        const mrb_callinfo *ci = frame_ci(mrb, f, &is_top);
        if (!ci) break;
        if (ci_irep(ci)) count++;
    }

    msgpack_pack_map(&pk, 1);
    fmrb_dbg_pack_key(&pk, "frames");
    msgpack_pack_array(&pk, count);

    int emitted = 0;
    for (int f = 0; f <= top && emitted < count; f++) {
        bool is_top;
        const mrb_callinfo *ci = frame_ci(mrb, f, &is_top);
        if (!ci) break;
        const mrb_irep *irep = ci_irep(ci);
        if (!irep) continue;   // skip C frames
        int32_t line = ci_line(mrb, ci, irep, is_top);
        const char *file = ci_file(mrb, ci, irep, is_top);
        const char *func = ci->mid ? mrb_sym_name(mrb, ci->mid) : "(top)";

        msgpack_pack_map(&pk, 4);
        fmrb_dbg_pack_kv_int(&pk, "idx", emitted);
        fmrb_dbg_pack_kv_str(&pk, "func", func ? func : "?");
        fmrb_dbg_pack_kv_str(&pk, "file", file ? file : "");
        fmrb_dbg_pack_kv_int(&pk, "line", line);
        emitted++;
    }

    park_resp_t r = { fb.overflow ? FMRB_ERR_NO_MEMORY : FMRB_OK,
                      d->park_buf, fb.len };
    return r;
}

// Safe type-based value formatter (no mrb_inspect / Ruby method calls).
static void format_value(mrb_state *mrb, mrb_value v, char *out, size_t cap,
                         const char **type, bool *truncated) {
    *truncated = false;
    switch (mrb_type(v)) {
        case MRB_TT_FALSE:
            if (mrb_nil_p(v)) { *type = "nil";   snprintf(out, cap, "nil"); }
            else              { *type = "false"; snprintf(out, cap, "false"); }
            break;
        case MRB_TT_TRUE:
            *type = "true"; snprintf(out, cap, "true");
            break;
        case MRB_TT_INTEGER:
            *type = "Integer";
            snprintf(out, cap, "%lld", (long long)mrb_integer(v));
            break;
#ifndef MRB_NO_FLOAT
        case MRB_TT_FLOAT:
            *type = "Float";
            snprintf(out, cap, "%g", (double)mrb_float(v));
            break;
#endif
        case MRB_TT_SYMBOL: {
            *type = "Symbol";
            const char *n = mrb_sym_name(mrb, mrb_symbol(v));
            snprintf(out, cap, ":%s", n ? n : "?");
            break;
        }
        case MRB_TT_STRING: {
            *type = "String";
            mrb_int len = RSTRING_LEN(v);
            const char *p = RSTRING_PTR(v);
            size_t n = (len > 64) ? 64 : (size_t)len;
            if (n >= cap - 3) n = cap - 3;
            out[0] = '"';
            memcpy(out + 1, p, n);
            out[1 + n] = '"';
            out[2 + n] = '\0';
            *truncated = (len > (mrb_int)n);
            break;
        }
        case MRB_TT_ARRAY:
            *type = "Array";
            snprintf(out, cap, "Array(len=%lld)", (long long)RARRAY_LEN(v));
            break;
        case MRB_TT_HASH:
            *type = "Hash";
            snprintf(out, cap, "Hash(size=%lld)", (long long)mrb_hash_size(mrb, v));
            break;
        default: {
            *type = "Object";
            const char *cn = mrb_obj_classname(mrb, v);
            snprintf(out, cap, "#<%s>", cn ? cn : "Object");
            break;
        }
    }
}

// Count a value's instance variables without allocating (mrb_iv_foreach).
static int iv_count_cb(mrb_state *mrb, mrb_sym sym, mrb_value v, void *p) {
    (void)mrb; (void)sym; (void)v;
    (*(int *)p)++;
    return 0;
}

// True if `v` has children worth expanding in the variables pane.
static bool is_expandable(mrb_state *mrb, mrb_value v) {
    switch (mrb_type(v)) {
        case MRB_TT_ARRAY: return RARRAY_LEN(v) > 0;
        case MRB_TT_HASH:  return mrb_hash_size(mrb, v) > 0;
        default:
            if (mrb_immediate_p(v)) return false;
            int n = 0;
            mrb_iv_foreach(mrb, v, iv_count_cb, &n);
            return n > 0;
    }
}

// Reserve an expand handle for `v` (1-based). 0 = not expandable / table full.
static int expand_handle_for(fmrb_debug_ctx_t *d, mrb_state *mrb, mrb_value v) {
    if (!is_expandable(mrb, v)) return 0;
    if (d->nhandles >= MAX_HANDLES) return 0;
    d->handles[d->nhandles] = v;
    return ++d->nhandles;
}

// Pack one {name,type,value,truncated,ref} entry. `ref` > 0 means expandable.
static void pack_one_var(msgpack_packer *pk, fmrb_debug_ctx_t *d,
                         mrb_state *mrb, const char *name, mrb_value v) {
    char valbuf[96];
    const char *type = "nil";
    bool truncated = false;
    format_value(mrb, v, valbuf, sizeof(valbuf), &type, &truncated);
    int ref = expand_handle_for(d, mrb, v);
    msgpack_pack_map(pk, 5);
    fmrb_dbg_pack_kv_str(pk, "name", name ? name : "?");
    fmrb_dbg_pack_kv_str(pk, "type", type);
    fmrb_dbg_pack_kv_str(pk, "value", valbuf);
    fmrb_dbg_pack_kv_bool(pk, "truncated", truncated);
    fmrb_dbg_pack_kv_int(pk, "ref", ref);
}

// Pack the trailing "(N more)" summary entry for truncated containers.
static void pack_more_note(msgpack_packer *pk, long remaining) {
    char note[32];
    snprintf(note, sizeof(note), "(%ld more)", remaining);
    msgpack_pack_map(pk, 5);
    fmrb_dbg_pack_kv_str(pk, "name", "...");
    fmrb_dbg_pack_kv_str(pk, "type", "");
    fmrb_dbg_pack_kv_str(pk, "value", note);
    fmrb_dbg_pack_kv_bool(pk, "truncated", true);
    fmrb_dbg_pack_kv_int(pk, "ref", 0);
}

// Build {"vars":[{name,type,value,truncated,ref}...]} into d->park_buf.
static park_resp_t build_frame_vars(fmrb_debug_ctx_t *d, int frame) {
    mrb_state *mrb = d->mrb;
    bool is_top;
    const mrb_callinfo *ci = frame_ci(mrb, frame, &is_top);
    const mrb_irep *irep = ci ? ci_irep(ci) : NULL;

    fixbuf_t fb = { d->park_buf, sizeof(d->park_buf), 0, false };
    msgpack_packer pk;
    msgpack_packer_init(&pk, &fb, fixbuf_write);

    if (!irep) {
        msgpack_pack_map(&pk, 1);
        fmrb_dbg_pack_key(&pk, "vars");
        msgpack_pack_array(&pk, 0);
        park_resp_t r = { FMRB_OK, d->park_buf, fb.len };
        return r;
    }

    mrb_value *regs = ci->stack;
    int nlocals = irep->nlocals;

    // Count named locals (skip anonymous / empty-named entries).
    int nvars = 0;
    for (int reg = 1; reg < nlocals; reg++) {
        mrb_sym sym = irep->lv ? irep->lv[reg - 1] : 0;
        if (!sym) continue;
        const char *nm = mrb_sym_name(mrb, sym);
        if (nm && nm[0]) nvars++;
    }

    msgpack_pack_map(&pk, 1);
    fmrb_dbg_pack_key(&pk, "vars");
    msgpack_pack_array(&pk, nvars);

    int arena = mrb_gc_arena_save(mrb);
    for (int reg = 1; reg < nlocals; reg++) {
        mrb_sym sym = irep->lv ? irep->lv[reg - 1] : 0;
        if (!sym) continue;
        const char *name = mrb_sym_name(mrb, sym);
        if (!name || !name[0]) continue;
        pack_one_var(&pk, d, mrb, name, regs[reg]);
        mrb_gc_arena_restore(mrb, arena);
    }

    park_resp_t r = { fb.overflow ? FMRB_ERR_NO_MEMORY : FMRB_OK,
                      d->park_buf, fb.len };
    return r;
}

// Context for packing object instance variables during mrb_iv_foreach.
typedef struct {
    msgpack_packer   *pk;
    fmrb_debug_ctx_t *d;
    mrb_state        *mrb;
    int               remaining;   // slots still to pack
    int               arena;
} iv_pack_t;

static int iv_pack_cb(mrb_state *mrb, mrb_sym sym, mrb_value v, void *p) {
    iv_pack_t *c = (iv_pack_t *)p;
    if (c->remaining <= 0) return 1;   // stop iteration
    const char *name = mrb_sym_name(mrb, sym);
    pack_one_var(c->pk, c->d, mrb, name, v);
    mrb_gc_arena_restore(mrb, c->arena);
    c->remaining--;
    return 0;
}

// Build {"vars":[...]} for the children of a previously handed-out handle.
static park_resp_t build_expand(fmrb_debug_ctx_t *d, int handle) {
    mrb_state *mrb = d->mrb;
    fixbuf_t fb = { d->park_buf, sizeof(d->park_buf), 0, false };
    msgpack_packer pk;
    msgpack_packer_init(&pk, &fb, fixbuf_write);

    if (handle < 1 || handle > d->nhandles) {
        msgpack_pack_map(&pk, 1);
        fmrb_dbg_pack_key(&pk, "vars");
        msgpack_pack_array(&pk, 0);
        park_resp_t r = { FMRB_ERR_INVALID_PARAM, d->park_buf, fb.len };
        return r;
    }
    mrb_value v = d->handles[handle - 1];

    msgpack_pack_map(&pk, 1);
    fmrb_dbg_pack_key(&pk, "vars");

    switch (mrb_type(v)) {
        case MRB_TT_ARRAY: {
            long len = (long)RARRAY_LEN(v);
            int shown = len > MAX_CHILDREN ? MAX_CHILDREN : (int)len;
            bool more = len > shown;
            msgpack_pack_array(&pk, shown + (more ? 1 : 0));
            int arena = mrb_gc_arena_save(mrb);
            for (int i = 0; i < shown; i++) {
                char nm[16];
                snprintf(nm, sizeof(nm), "[%d]", i);
                pack_one_var(&pk, d, mrb, nm, mrb_ary_entry(v, i));
                mrb_gc_arena_restore(mrb, arena);
            }
            if (more) pack_more_note(&pk, len - shown);
            break;
        }
        case MRB_TT_HASH: {
            int arena0 = mrb_gc_arena_save(mrb);
            mrb_value keys = mrb_hash_keys(mrb, v);   // temp array (rooted below)
            long n = (long)RARRAY_LEN(keys);
            int shown = n > MAX_CHILDREN ? MAX_CHILDREN : (int)n;
            bool more = n > shown;
            msgpack_pack_array(&pk, shown + (more ? 1 : 0));
            int arena1 = mrb_gc_arena_save(mrb);
            for (int i = 0; i < shown; i++) {
                mrb_value key = mrb_ary_entry(keys, i);
                mrb_value val = mrb_hash_get(mrb, v, key);
                char nm[48];
                const char *kt = "nil";
                bool ktr = false;
                format_value(mrb, key, nm, sizeof(nm), &kt, &ktr);
                pack_one_var(&pk, d, mrb, nm, val);
                mrb_gc_arena_restore(mrb, arena1);
            }
            if (more) pack_more_note(&pk, n - shown);
            mrb_gc_arena_restore(mrb, arena0);
            break;
        }
        default: {
            int total = 0;
            mrb_iv_foreach(mrb, v, iv_count_cb, &total);
            int shown = total > MAX_CHILDREN ? MAX_CHILDREN : total;
            bool more = total > shown;
            msgpack_pack_array(&pk, shown + (more ? 1 : 0));
            iv_pack_t c = { &pk, d, mrb, shown, mrb_gc_arena_save(mrb) };
            mrb_iv_foreach(mrb, v, iv_pack_cb, &c);
            if (more) pack_more_note(&pk, total - shown);
            break;
        }
    }

    park_resp_t r = { fb.overflow ? FMRB_ERR_NO_MEMORY : FMRB_OK,
                      d->park_buf, fb.len };
    return r;
}

// ==========================================================================
// park loop (runs on the VM task, inside the hook)
// ==========================================================================
static void park(fmrb_debug_ctx_t *d, int reason, const mrb_irep *irep,
                 uint32_t pc_off, int32_t line, mrb_value *regs, int bp_id) {
    mrb_state *mrb = d->mrb;
    d->stopped   = true;
    d->at_stop   = true;
    d->stop_irep = irep;
    d->stop_line = line;
    d->cur_regs  = regs;
    d->nhandles  = 0;            // expand handles are only valid within a stop

    const char *file = mrb_debug_get_filename(mrb, irep, pc_off);
    emit_event(FMRB_DBG_EV_STOPPED, d->pid, reason, bp_id, line, file);

    for (;;) {
        park_cmd_t cmd;
        if (fmrb_queue_receive(d->cmd_q, &cmd, FMRB_MS_TO_TICKS(200)) != pdTRUE) {
            if (!d->armed) break;   // detached out from under us
            continue;
        }
        if (cmd.op == PARK_CONTINUE || cmd.op == PARK_DETACH) {
            d->step_mode = FMRB_STEP_NONE;
            break;
        } else if (cmd.op == PARK_STEP_IN || cmd.op == PARK_STEP_OVER ||
                   cmd.op == PARK_STEP_OUT) {
            d->step_mode = (cmd.op == PARK_STEP_IN)   ? FMRB_STEP_IN :
                           (cmd.op == PARK_STEP_OVER) ? FMRB_STEP_OVER : FMRB_STEP_OUT;
            d->step_ci = mrb->c->ci;
            d->step_c  = mrb->c;
            break;
        } else if (cmd.op == PARK_STACK_TRACE) {
            park_resp_t r = build_stack_trace(d, cmd.arg);
            fmrb_queue_send(d->resp_q, &r, FMRB_MS_TO_TICKS(100));
        } else if (cmd.op == PARK_FRAME_VARS) {
            park_resp_t r = build_frame_vars(d, cmd.arg);
            fmrb_queue_send(d->resp_q, &r, FMRB_MS_TO_TICKS(100));
        } else if (cmd.op == PARK_EXPAND) {
            park_resp_t r = build_expand(d, cmd.arg);
            fmrb_queue_send(d->resp_q, &r, FMRB_MS_TO_TICKS(100));
        }
    }

    d->stopped   = false;
    d->pause_req = false;
    emit_event(FMRB_DBG_EV_RESUMED, d->pid, 0, -1, 0, NULL);
}

// ==========================================================================
// the hook (runs on the VM task, every instruction fetch)
// ==========================================================================
static void code_fetch_hook(mrb_state *mrb, const struct mrb_irep *irep,
                            const mrb_code *pc, mrb_value *regs) {
    fmrb_debug_ctx_t *d = ctx_by_mrb(mrb);
    if (!d || !d->armed) return;                 // fast path
    if (!irep || !irep->debug_info) return;      // no line info

    uint32_t pc_off = (uint32_t)(pc - irep->iseq);
    int32_t line = mrb_debug_get_line(mrb, irep, pc_off);
    if (line < 0) return;

    // Clear the "just resumed on this line" latch once we leave that line.
    if (d->at_stop && (irep != d->stop_irep || line != d->stop_line)) {
        d->at_stop = false;
    }

    bool line_start = is_line_start(mrb, irep, pc_off, line);
    int reason = -1;
    int bp_id = -1;

    if (d->pause_req) {
        reason = FMRB_STOP_PAUSE;
    } else if (d->step_mode != FMRB_STEP_NONE && mrb->c == d->step_c) {
        // mruby-task safety: only judge steps in the context we started in.
        bool depth_ok = true;
        if (d->step_mode == FMRB_STEP_OVER) {
            depth_ok = ((intptr_t)mrb->c->ci <= (intptr_t)d->step_ci);
        } else if (d->step_mode == FMRB_STEP_OUT) {
            depth_ok = ((intptr_t)mrb->c->ci < (intptr_t)d->step_ci);
        }
        if (depth_ok && line_start && !d->at_stop) {
            reason = FMRB_STOP_STEP;
        }
    }

    if (reason < 0 && line_start && !d->at_stop) {
        const char *file = mrb_debug_get_filename(mrb, irep, pc_off);
        if (file) {
            int id = bp_match(d, file, line);
            if (id >= 0) { reason = FMRB_STOP_BREAKPOINT; bp_id = id; }
        }
    }

    if (reason < 0) return;
    park(d, reason, irep, pc_off, line, regs, bp_id);
}

// ==========================================================================
// public API (debugd task)
// ==========================================================================
fmrb_err_t fmrb_debug_ctx_init(void) {
    if (!s_event_q) {
        s_event_q = fmrb_queue_create(EVENT_Q_DEPTH, sizeof(fmrb_dbg_event_t));
        if (!s_event_q) return FMRB_ERR_NO_MEMORY;
    }
    return FMRB_OK;
}

fmrb_err_t fmrb_debug_ctx_attach(int pid) {
    if (ctx_by_pid(pid)) return FMRB_OK;   // already attached

    fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id(pid);
    if (!ctx) return FMRB_ERR_NOT_FOUND;
    if (ctx->vm_type != FMRB_VM_TYPE_MRUBY || !ctx->mrb) return FMRB_ERR_NOT_SUPPORTED;
    if (ctx->state != PROC_STATE_RUNNING && ctx->state != PROC_STATE_SUSPENDED) {
        return FMRB_ERR_INVALID_STATE;
    }

    fmrb_debug_ctx_t *d = NULL;
    for (int i = 0; i < FMRB_DEBUG_MAX_ATTACH; i++) {
        if (!s_dctx[i].in_use) { d = &s_dctx[i]; break; }
    }
    if (!d) return FMRB_ERR_NO_RESOURCE;

    memset(d, 0, sizeof(*d));
    d->cmd_q  = fmrb_queue_create(4, sizeof(park_cmd_t));
    d->resp_q = fmrb_queue_create(4, sizeof(park_resp_t));
    if (!d->cmd_q || !d->resp_q) { ctx_free(d); return FMRB_ERR_NO_MEMORY; }

    d->pid = pid;
    d->mrb = ctx->mrb;
    d->gen = ctx->gen;
    d->next_bp_id = 1;
    d->in_use = true;

    // Install the hook only after the ctx is fully initialized (VM task reads
    // either NULL or a valid pointer; the 1-word store is atomic enough).
    // Cast to the field's exact type: picoruby.h does `#define mrb_irep
    // mrc_irep`, so the field (mrb_irep) and our function (mrc_irep) differ
    // only nominally; they are layout-compatible and the VM passes mrc_irep.
    d->mrb->code_fetch_hook =
        (__typeof__(d->mrb->code_fetch_hook))code_fetch_hook;
    d->armed = true;
    FMRB_LOGI(TAG, "attached pid=%d mrb=%p", pid, (void *)d->mrb);
    return FMRB_OK;
}

fmrb_err_t fmrb_debug_ctx_detach(int pid) {
    fmrb_debug_ctx_t *d = ctx_by_pid(pid);
    if (!d) return FMRB_ERR_NOT_FOUND;

    d->armed = false;
    for (int i = 0; i < FMRB_DEBUG_MAX_BP; i++) d->bps[i].enabled = false;

    // If parked, wake it so it resumes and unwinds the hook.
    if (d->stopped) {
        park_cmd_t c = { PARK_DETACH, 0 };
        fmrb_queue_send(d->cmd_q, &c, FMRB_MS_TO_TICKS(100));
        for (int i = 0; i < 100 && d->stopped; i++) fmrb_task_delay_ms(5);
    }

    // Detach the hook. Re-fetch to avoid touching a freed mrb.
    fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id(pid);
    if (ctx && ctx->gen == d->gen && ctx->mrb == d->mrb) {
        d->mrb->code_fetch_hook = NULL;
    }
    FMRB_LOGI(TAG, "detached pid=%d", pid);
    ctx_free(d);
    return FMRB_OK;
}

bool fmrb_debug_ctx_is_attached(int pid) {
    return ctx_by_pid(pid) != NULL;
}

void fmrb_debug_ctx_detach_all(void) {
    for (int i = 0; i < FMRB_DEBUG_MAX_ATTACH; i++) {
        if (s_dctx[i].in_use) fmrb_debug_ctx_detach(s_dctx[i].pid);
    }
}

fmrb_err_t fmrb_debug_ctx_bp_set(int pid, const char *file, int line, int *out_bp_id) {
    fmrb_debug_ctx_t *d = ctx_valid(pid);
    if (!d) return FMRB_ERR_NOT_FOUND;
    for (int i = 0; i < FMRB_DEBUG_MAX_BP; i++) {
        if (!d->bps[i].enabled) {
            // Populate id/line/file before arming the slot: the hook reads bps[]
            // concurrently while the VM runs, so `enabled` must be the last write
            // to avoid observing a half-initialized breakpoint.
            d->bps[i].id = d->next_bp_id++;
            d->bps[i].line = line;
            strncpy(d->bps[i].file, file, sizeof(d->bps[i].file) - 1);
            d->bps[i].file[sizeof(d->bps[i].file) - 1] = '\0';
            d->bps[i].enabled = true;
            if (out_bp_id) *out_bp_id = d->bps[i].id;
            return FMRB_OK;
        }
    }
    return FMRB_ERR_NO_RESOURCE;
}

fmrb_err_t fmrb_debug_ctx_bp_clear(int pid, int bp_id) {
    fmrb_debug_ctx_t *d = ctx_valid(pid);
    if (!d) return FMRB_ERR_NOT_FOUND;
    for (int i = 0; i < FMRB_DEBUG_MAX_BP; i++) {
        if (d->bps[i].enabled && (bp_id < 0 || d->bps[i].id == bp_id)) {
            d->bps[i].enabled = false;
        }
    }
    return FMRB_OK;
}

fmrb_err_t fmrb_debug_ctx_pause(int pid) {
    fmrb_debug_ctx_t *d = ctx_valid(pid);
    if (!d) return FMRB_ERR_NOT_FOUND;
    if (d->stopped) return FMRB_OK;
    d->pause_req = true;
    return FMRB_OK;
}

fmrb_err_t fmrb_debug_ctx_continue(int pid) {
    fmrb_debug_ctx_t *d = ctx_valid(pid);
    if (!d) return FMRB_ERR_NOT_FOUND;
    if (!d->stopped) return FMRB_ERR_INVALID_STATE;
    park_cmd_t c = { PARK_CONTINUE, 0 };
    fmrb_queue_send(d->cmd_q, &c, FMRB_MS_TO_TICKS(100));
    return FMRB_OK;
}

fmrb_err_t fmrb_debug_ctx_step(int pid, int step_mode) {
    fmrb_debug_ctx_t *d = ctx_valid(pid);
    if (!d) return FMRB_ERR_NOT_FOUND;
    if (!d->stopped) return FMRB_ERR_INVALID_STATE;
    park_op_t op = (step_mode == FMRB_STEP_IN)   ? PARK_STEP_IN :
                   (step_mode == FMRB_STEP_OUT)  ? PARK_STEP_OUT : PARK_STEP_OVER;
    park_cmd_t c = { op, 0 };
    fmrb_queue_send(d->cmd_q, &c, FMRB_MS_TO_TICKS(100));
    return FMRB_OK;
}

static fmrb_err_t inspect(int pid, park_op_t op, int arg,
                          const uint8_t **out_body, size_t *out_len) {
    fmrb_debug_ctx_t *d = ctx_valid(pid);
    if (!d) return FMRB_ERR_NOT_FOUND;
    if (!d->stopped) return FMRB_ERR_INVALID_STATE;

    park_cmd_t c = { op, arg };
    if (fmrb_queue_send(d->cmd_q, &c, FMRB_MS_TO_TICKS(100)) != pdTRUE) {
        return FMRB_ERR_TIMEOUT;
    }
    park_resp_t r;
    if (fmrb_queue_receive(d->resp_q, &r, FMRB_MS_TO_TICKS(2000)) != pdTRUE) {
        return FMRB_ERR_TIMEOUT;
    }
    *out_body = r.body;
    *out_len  = r.len;
    return r.err;
}

fmrb_err_t fmrb_debug_ctx_stack_trace(int pid, int max_frames,
                                      const uint8_t **out_body, size_t *out_len) {
    return inspect(pid, PARK_STACK_TRACE, max_frames, out_body, out_len);
}

fmrb_err_t fmrb_debug_ctx_frame_vars(int pid, int frame,
                                     const uint8_t **out_body, size_t *out_len) {
    return inspect(pid, PARK_FRAME_VARS, frame, out_body, out_len);
}

fmrb_err_t fmrb_debug_ctx_expand(int pid, int handle,
                                 const uint8_t **out_body, size_t *out_len) {
    return inspect(pid, PARK_EXPAND, handle, out_body, out_len);
}

bool fmrb_debug_ctx_poll_event(fmrb_dbg_event_t *out, uint32_t timeout_ms) {
    if (!s_event_q) return false;
    return fmrb_queue_receive(s_event_q, out, FMRB_MS_TO_TICKS(timeout_ms)) == pdTRUE;
}
