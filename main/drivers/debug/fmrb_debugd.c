// Remote debugger daemon task. See fmrb_debugd.h and
// doc/remote_debug/vm_remote_debug_protocol.md.
//
// Phase 1 scope in THIS file: the task loop, the transport wiring, and the
// non-hook commands (version / ps / log_read / kill / stop / suspend / resume /
// spawn). The hook-based commands (attach / detach / breakpoints / pause /
// step / stack_trace / frame_vars) are dispatched to fmrb_debug_ctx once that
// module lands; until then they return FMRB_ERR_NOT_SUPPORTED.
#include "fmrb_debugd.h"
#include "fmrb_debug_proto.h"
#include "fmrb_debug_transport.h"
#include "fmrb_debug_ctx.h"

#include <string.h>

#include "fmrb.h"
#include "fmrb_app.h"
#include "fmrb_log.h"
#include "fmrb_log_buffer.h"
#include "fmrb_rtos.h"
#include "fmrb_task_config.h"

static const char *TAG = "debugd";

// Transport selection. Linux uses TCP; ESP32 targets use the BLE debug GATT
// service. Everything above this line speaks bare msgpack bodies either way.
#ifdef CONFIG_IDF_TARGET_LINUX
static const fmrb_debug_transport_ops_t *s_tp = &fmrb_debug_transport_tcp;
#else
static const fmrb_debug_transport_ops_t *s_tp = &fmrb_debug_transport_ble;
#endif
static bool s_started;

// Session owner (Phase E0). Shared between the debugd task and VM tasks (the
// FMRB::Debug gem), so accessed with __atomic_* (acquire/release) rather than
// a plain read. Stored as int; values are fmrb_dbg_owner_t.
static int s_owner = FMRB_DBG_OWNER_NONE;

// Compare-and-swap the owner. Returns true if the swap happened.
static bool owner_cas(fmrb_dbg_owner_t expect, fmrb_dbg_owner_t desired) {
    int e = (int)expect;
    return __atomic_compare_exchange_n(&s_owner, &e, (int)desired, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

// Request reassembly target (one frame at a time, single task).
FMRB_DBG_BSS_ATTR static uint8_t s_rx_body[FMRB_DEBUG_MAX_FRAME];

static void send_writer(fmrb_dbg_writer_t *w) {
    size_t len = 0;
    const uint8_t *body = fmrb_dbg_writer_body(w, &len);
    s_tp->send(body, len);
}

// Reply with a bare {ok}/nil response carrying err.
static void reply_ok(const fmrb_dbg_req_t *req, int err) {
    fmrb_dbg_writer_t w;
    fmrb_dbg_writer_init(&w);
    fmrb_dbg_write_ok(&w, req->seq, err);
    send_writer(&w);
    fmrb_dbg_writer_destroy(&w);
}

// --- non-hook command handlers --------------------------------------------

static void handle_version(const fmrb_dbg_req_t *req) {
    fmrb_dbg_writer_t w;
    fmrb_dbg_writer_init(&w);
    fmrb_dbg_resp_begin(&w, req->seq, FMRB_OK);
    msgpack_pack_map(&w.pk, 2);
    fmrb_dbg_pack_kv_int(&w.pk, "proto", FMRB_DEBUG_PROTO_VER);
    fmrb_dbg_pack_kv_str(&w.pk, "fw", FMRB_OS_VERSION);
    send_writer(&w);
    fmrb_dbg_writer_destroy(&w);
}

static void handle_ps(const fmrb_dbg_req_t *req) {
    fmrb_app_info_t list[FMRB_MAX_APPS];
    int32_t n = fmrb_app_ps(list, FMRB_MAX_APPS);
    if (n < 0) n = 0;

    fmrb_dbg_writer_t w;
    fmrb_dbg_writer_init(&w);
    fmrb_dbg_resp_begin(&w, req->seq, FMRB_OK);
    msgpack_pack_map(&w.pk, 1);
    fmrb_dbg_pack_key(&w.pk, "apps");
    msgpack_pack_array(&w.pk, n);
    for (int32_t i = 0; i < n; i++) {
        msgpack_pack_map(&w.pk, 7);
        fmrb_dbg_pack_kv_int(&w.pk, "pid",       list[i].app_id);
        fmrb_dbg_pack_kv_str(&w.pk, "name",      list[i].app_name);
        fmrb_dbg_pack_kv_int(&w.pk, "state",     list[i].state);
        fmrb_dbg_pack_kv_int(&w.pk, "vm",        list[i].vm_type);
        fmrb_dbg_pack_kv_int(&w.pk, "mem_used",  (int64_t)list[i].mem_used);
        fmrb_dbg_pack_kv_int(&w.pk, "mem_total", (int64_t)list[i].mem_total);
        fmrb_dbg_pack_kv_int(&w.pk, "stack_hw",  (int64_t)list[i].stack_high_water);
        /* Spinel begin/catch depth high-waters (0 for non-NATIVE VMs) */
        fmrb_dbg_pack_kv_int(&w.pk, "exc_hw",    list[i].exc_hw);
        fmrb_dbg_pack_kv_int(&w.pk, "catch_hw",  list[i].catch_hw);
    }
    send_writer(&w);
    fmrb_dbg_writer_destroy(&w);
}

static void handle_log_read(const fmrb_dbg_req_t *req) {
    // PSRAM: filled by memcpy from the log ring and consumed by msgpack,
    // both CPU-only (doc/internal_ram_budget.md E).
    EXT_RAM_BSS_ATTR static char linebuf[2048];
    uint32_t pos = req->pos;
    uint32_t before = pos;
    int max_lines = req->max_lines > 0 ? req->max_lines : 50;

    int n = fmrb_log_buffer_read_lines(linebuf, sizeof(linebuf), max_lines, &pos);
    if (n <= 0) linebuf[0] = '\0';

    // Ring overrun: read_pos advanced more than the bytes we actually got back
    // (see ble_task.c ble_fs_poll_logs for the same heuristic).
    size_t bin_len = (n > 0) ? strlen(linebuf) : 0;
    uint32_t actual_advance = pos - before;
    uint32_t expected_advance = (uint32_t)(bin_len + (size_t)(n > 0 ? n : 0));
    bool overrun = (actual_advance > expected_advance);

    fmrb_dbg_writer_t w;
    fmrb_dbg_writer_init(&w);
    fmrb_dbg_resp_begin(&w, req->seq, FMRB_OK);
    msgpack_pack_map(&w.pk, 3);
    fmrb_dbg_pack_kv_str(&w.pk, "lines", linebuf);
    fmrb_dbg_pack_kv_int(&w.pk, "pos", (int64_t)pos);
    fmrb_dbg_pack_kv_bool(&w.pk, "overrun", overrun);
    send_writer(&w);
    fmrb_dbg_writer_destroy(&w);
}

// kill / stop / suspend / resume all map to a bool-returning fmrb_app API.
static void handle_app_ctl(const fmrb_dbg_req_t *req) {
    if (!req->have_pid) { reply_ok(req, FMRB_ERR_INVALID_PARAM); return; }
    // A debugged VM must not be killed/stopped/suspended out from under the
    // debugger (it may be parked). Require detach first.
    if (fmrb_debug_ctx_is_attached(req->pid) &&
        (req->cmd == DBG_CMD_KILL || req->cmd == DBG_CMD_STOP ||
         req->cmd == DBG_CMD_SUSPEND)) {
        reply_ok(req, FMRB_ERR_BUSY);
        return;
    }
    bool ok = false;
    switch (req->cmd) {
        case DBG_CMD_KILL:    ok = fmrb_app_kill(req->pid);    break;
        case DBG_CMD_STOP:    ok = fmrb_app_stop(req->pid);    break;
        case DBG_CMD_SUSPEND: ok = fmrb_app_suspend(req->pid); break;
        case DBG_CMD_RESUME:  ok = fmrb_app_resume(req->pid);  break;
        default: break;
    }
    reply_ok(req, ok ? FMRB_OK : FMRB_ERR_FAILED);
}

// --- hook-based command handlers (delegate to fmrb_debug_ctx) --------------

static void handle_attach(const fmrb_dbg_req_t *req) {
    if (!req->have_pid) { reply_ok(req, FMRB_ERR_INVALID_PARAM); return; }
    fmrb_err_t err = fmrb_debug_ctx_attach(req->pid);
    if (err == FMRB_OK) {
        // Claim the remote session slot (NONE -> REMOTE). Kept until the client
        // disconnects (reset in the task loop), matching the VSCode session
        // lifetime and keeping local acquire out while a remote client is live.
        owner_cas(FMRB_DBG_OWNER_NONE, FMRB_DBG_OWNER_REMOTE);
    }
    reply_ok(req, err);
}

static void handle_detach(const fmrb_dbg_req_t *req) {
    if (!req->have_pid) { reply_ok(req, FMRB_ERR_INVALID_PARAM); return; }
    reply_ok(req, fmrb_debug_ctx_detach(req->pid));
}

static void handle_bp_set(const fmrb_dbg_req_t *req) {
    if (!req->have_pid || req->file[0] == '\0') { reply_ok(req, FMRB_ERR_INVALID_PARAM); return; }
    int bp_id = -1;
    fmrb_err_t err = fmrb_debug_ctx_bp_set(req->pid, req->file, req->line, &bp_id);

    fmrb_dbg_writer_t w;
    fmrb_dbg_writer_init(&w);
    if (err == FMRB_OK) {
        fmrb_dbg_resp_begin(&w, req->seq, FMRB_OK);
        msgpack_pack_map(&w.pk, 1);
        fmrb_dbg_pack_kv_int(&w.pk, "bp_id", bp_id);
    } else {
        fmrb_dbg_resp_begin(&w, req->seq, err);
        msgpack_pack_nil(&w.pk);
    }
    send_writer(&w);
    fmrb_dbg_writer_destroy(&w);
}

static void handle_bp_clear(const fmrb_dbg_req_t *req) {
    if (!req->have_pid) { reply_ok(req, FMRB_ERR_INVALID_PARAM); return; }
    reply_ok(req, fmrb_debug_ctx_bp_clear(req->pid, req->bp_id));
}

static void handle_flow(const fmrb_dbg_req_t *req) {
    if (!req->have_pid) { reply_ok(req, FMRB_ERR_INVALID_PARAM); return; }
    fmrb_err_t err;
    switch (req->cmd) {
        case DBG_CMD_PAUSE:     err = fmrb_debug_ctx_pause(req->pid);    break;
        case DBG_CMD_CONTINUE:  err = fmrb_debug_ctx_continue(req->pid); break;
        case DBG_CMD_STEP_IN:   err = fmrb_debug_ctx_step(req->pid, FMRB_STEP_IN);   break;
        case DBG_CMD_STEP_OVER: err = fmrb_debug_ctx_step(req->pid, FMRB_STEP_OVER); break;
        case DBG_CMD_STEP_OUT:  err = fmrb_debug_ctx_step(req->pid, FMRB_STEP_OUT);  break;
        default: err = FMRB_ERR_INVALID_PARAM; break;
    }
    reply_ok(req, err);
}

// stack_trace / frame_vars: the parked VM builds the payload map; we frame it.
static void handle_inspect(const fmrb_dbg_req_t *req) {
    if (!req->have_pid) { reply_ok(req, FMRB_ERR_INVALID_PARAM); return; }
    const uint8_t *body = NULL;
    size_t len = 0;
    fmrb_err_t err;
    if (req->cmd == DBG_CMD_STACK_TRACE) {
        err = fmrb_debug_ctx_stack_trace(req->pid, req->max, &body, &len);
    } else if (req->cmd == DBG_CMD_EXPAND) {
        err = fmrb_debug_ctx_expand(req->pid, req->handle, &body, &len);
    } else {
        err = fmrb_debug_ctx_frame_vars(req->pid, req->frame, &body, &len);
    }

    fmrb_dbg_writer_t w;
    fmrb_dbg_writer_init(&w);
    fmrb_dbg_resp_begin(&w, req->seq, err);
    if (err == FMRB_OK && body) {
        // Append the pre-built payload map as the 4th array element.
        msgpack_sbuffer_write(&w.sbuf, (const char *)body, len);
    } else {
        msgpack_pack_nil(&w.pk);
    }
    send_writer(&w);
    fmrb_dbg_writer_destroy(&w);
}

// --- event forwarding (hook -> debugd -> client) ---------------------------

static const char *stop_reason_str(int reason) {
    switch (reason) {
        case FMRB_STOP_BREAKPOINT: return "breakpoint";
        case FMRB_STOP_STEP:       return "step";
        case FMRB_STOP_PAUSE:      return "pause";
        default:                   return "unknown";
    }
}

static void forward_events(void) {
    // While the on-device gem owns the session it is the single event consumer;
    // draining here would steal its events (the queue has one consumer).
    if (fmrb_debugd_owner() == FMRB_DBG_OWNER_LOCAL) return;

    fmrb_dbg_event_t ev;
    while (fmrb_debug_ctx_poll_event(&ev, 0)) {
        if (!s_tp->connected()) continue;   // drain but drop when no client
        fmrb_dbg_writer_t w;
        fmrb_dbg_writer_init(&w);
        switch (ev.type) {
            case FMRB_DBG_EV_STOPPED:
                fmrb_dbg_event_begin(&w, "stopped");
                msgpack_pack_map(&w.pk, ev.bp_id >= 0 ? 5 : 4);
                fmrb_dbg_pack_kv_int(&w.pk, "pid", ev.pid);
                fmrb_dbg_pack_kv_str(&w.pk, "reason", stop_reason_str(ev.reason));
                fmrb_dbg_pack_kv_str(&w.pk, "file", ev.file);
                fmrb_dbg_pack_kv_int(&w.pk, "line", ev.line);
                if (ev.bp_id >= 0) fmrb_dbg_pack_kv_int(&w.pk, "bp_id", ev.bp_id);
                break;
            case FMRB_DBG_EV_RESUMED:
                fmrb_dbg_event_begin(&w, "resumed");
                msgpack_pack_map(&w.pk, 1);
                fmrb_dbg_pack_kv_int(&w.pk, "pid", ev.pid);
                break;
            case FMRB_DBG_EV_EXITED:
                fmrb_dbg_event_begin(&w, "exited");
                msgpack_pack_map(&w.pk, 1);
                fmrb_dbg_pack_kv_int(&w.pk, "pid", ev.pid);
                break;
        }
        send_writer(&w);
        fmrb_dbg_writer_destroy(&w);
    }
}

static void handle_spawn(const fmrb_dbg_req_t *req) {
    if (req->path[0] == '\0') { reply_ok(req, FMRB_ERR_INVALID_PARAM); return; }
    int32_t pid = -1;
    fmrb_err_t err = fmrb_app_spawn_app(req->path, &pid);

    fmrb_dbg_writer_t w;
    fmrb_dbg_writer_init(&w);
    if (err == FMRB_OK) {
        fmrb_dbg_resp_begin(&w, req->seq, FMRB_OK);
        msgpack_pack_map(&w.pk, 1);
        fmrb_dbg_pack_kv_int(&w.pk, "pid", pid);
    } else {
        fmrb_dbg_resp_begin(&w, req->seq, err);
        msgpack_pack_nil(&w.pk);
    }
    send_writer(&w);
    fmrb_dbg_writer_destroy(&w);
}

// --- dispatch --------------------------------------------------------------

// Hook-based commands drive the shared debug core (attach/park/inspect). They
// are refused while the on-device gem owns the session; the non-hook commands
// (version/ps/log_read/spawn/app-ctl) stay available to remote clients.
static bool is_hook_cmd(int cmd) {
    switch (cmd) {
        case DBG_CMD_ATTACH:
        case DBG_CMD_DETACH:
        case DBG_CMD_BP_SET:
        case DBG_CMD_BP_CLEAR:
        case DBG_CMD_PAUSE:
        case DBG_CMD_CONTINUE:
        case DBG_CMD_STEP_IN:
        case DBG_CMD_STEP_OVER:
        case DBG_CMD_STEP_OUT:
        case DBG_CMD_STACK_TRACE:
        case DBG_CMD_FRAME_VARS:
        case DBG_CMD_EXPAND:
            return true;
        default:
            return false;
    }
}

static void dispatch(const fmrb_dbg_req_t *req) {
    if (is_hook_cmd(req->cmd) && fmrb_debugd_owner() == FMRB_DBG_OWNER_LOCAL) {
        reply_ok(req, FMRB_ERR_BUSY);   // on-device debug session holds the core
        return;
    }
    switch (req->cmd) {
        case DBG_CMD_VERSION:  handle_version(req);  break;
        case DBG_CMD_PS:       handle_ps(req);       break;
        case DBG_CMD_LOG_READ: handle_log_read(req); break;
        case DBG_CMD_KILL:
        case DBG_CMD_STOP:
        case DBG_CMD_SUSPEND:
        case DBG_CMD_RESUME:   handle_app_ctl(req);  break;
        case DBG_CMD_SPAWN:    handle_spawn(req);    break;

        case DBG_CMD_ATTACH:   handle_attach(req);   break;
        case DBG_CMD_DETACH:   handle_detach(req);   break;
        case DBG_CMD_BP_SET:   handle_bp_set(req);   break;
        case DBG_CMD_BP_CLEAR: handle_bp_clear(req); break;
        case DBG_CMD_PAUSE:
        case DBG_CMD_CONTINUE:
        case DBG_CMD_STEP_IN:
        case DBG_CMD_STEP_OVER:
        case DBG_CMD_STEP_OUT:  handle_flow(req);    break;
        case DBG_CMD_STACK_TRACE:
        case DBG_CMD_FRAME_VARS:
        case DBG_CMD_EXPAND:     handle_inspect(req); break;

        case DBG_CMD_UNKNOWN:
        default:
            reply_ok(req, FMRB_ERR_INVALID_PARAM);
            break;
    }
}

// --- task ------------------------------------------------------------------

static void debugd_main(void *arg) {
    (void)arg;
    if (fmrb_debug_ctx_init() != FMRB_OK) {
        FMRB_LOGE(TAG, "ctx init failed; debugd not running");
        fmrb_task_delete(NULL);
        return;
    }
    if (s_tp->init() != FMRB_OK) {
        FMRB_LOGE(TAG, "transport init failed; debugd not running");
        fmrb_task_delete(NULL);
        return;
    }
    FMRB_LOGI(TAG, "debugd task started");

    bool was_connected = false;
    for (;;) {
        int r = s_tp->poll(s_rx_body, sizeof(s_rx_body), 50);
        if (r > 0) {
            fmrb_dbg_req_t req;
            if (fmrb_dbg_proto_decode_req(s_rx_body, (size_t)r, &req) == FMRB_OK) {
                dispatch(&req);
            } else {
                FMRB_LOGW(TAG, "malformed request frame (%d bytes)", r);
            }
        }

        // Forward any stopped/resumed/exited events from the hooks.
        forward_events();

        // On client disconnect, detach every VM so the debugger dying never
        // leaves a VM parked, and drop the remote session slot. Skip both while
        // the on-device gem owns the session: the remote transport holds no
        // attachments then, and detach_all would tear down the gem's targets.
        bool now = s_tp->connected();
        if (now != was_connected) {
            FMRB_LOGI(TAG, "client %s", now ? "up" : "down");
            if (!now && fmrb_debugd_owner() != FMRB_DBG_OWNER_LOCAL) {
                fmrb_debug_ctx_detach_all();
                owner_cas(FMRB_DBG_OWNER_REMOTE, FMRB_DBG_OWNER_NONE);
            }
            was_connected = now;
        }
    }
}

void fmrb_debugd_init(void) {
    if (s_started) return;
    s_started = true;
    fmrb_task_handle_t handle;
    fmrb_task_create_ex(debugd_main, "debugd", FMRB_DEBUGD_TASK_STACK_SIZE,
                        NULL, FMRB_DEBUGD_TASK_PRIORITY, &handle,
                        FMRB_DEBUGD_TASK_FLAGS);
}

// --- session owner (Phase E0) ---------------------------------------------

fmrb_err_t fmrb_debugd_acquire_local(void) {
    if (owner_cas(FMRB_DBG_OWNER_NONE, FMRB_DBG_OWNER_LOCAL)) return FMRB_OK;
    // Re-acquiring an already-held local session is idempotent; a remote client
    // holding it is FMRB_ERR_BUSY.
    return (fmrb_debugd_owner() == FMRB_DBG_OWNER_LOCAL) ? FMRB_OK : FMRB_ERR_BUSY;
}

void fmrb_debugd_release_local(void) {
    if (fmrb_debugd_owner() != FMRB_DBG_OWNER_LOCAL) return;
    // Detach every VM before dropping ownership so a leaked gem session can
    // never leave a target parked. forward_events stays parked-out until the
    // owner clears, so debugd will not race us on the event queue here.
    fmrb_debug_ctx_detach_all();
    owner_cas(FMRB_DBG_OWNER_LOCAL, FMRB_DBG_OWNER_NONE);
}

fmrb_dbg_owner_t fmrb_debugd_owner(void) {
    return (fmrb_dbg_owner_t)__atomic_load_n(&s_owner, __ATOMIC_ACQUIRE);
}
