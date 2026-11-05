// Remote debugger daemon task. See fmrb_debugd.h and
// doc/remote_debug/vm_remote_debug_protocol.md.
//
// Phase 1 scope in THIS file: the task loop, the transport wiring, and the
// non-hook commands (version / ps / log_read / kill / stop / suspend / resume /
// spawn / audio / gc). The hook-based commands (attach / detach / breakpoints /
// pause / step / stack_trace / frame_vars) are dispatched to fmrb_debug_ctx once that
// module lands; until then they return FMRB_ERR_NOT_SUPPORTED.
#include "fmrb_debugd.h"
#include "fmrb_attr.h"
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
#include "host_task.h"
#include "fmrb_hal_time.h"
#ifdef CONFIG_IDF_TARGET_LINUX
#include "fmrb_mem.h"
#include "fmrb_toml.h"
#endif

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
        msgpack_pack_map(&w.pk, 9);
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
    // both CPU-only (doc/reference/internal_ram_budget.md E).
    FMRB_EXT_RAM_BSS_ATTR static char linebuf[2048];
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

// audio {on: 0|1, volume: 0-10}: each key that is present is set (mute,
// volume); with neither it only asks. The answer is {muted, volume}. Here so
// the simulation (which has no devctl) can be silenced from a tool, the way
// devctl's /audio/* does it on the boards.
static void handle_audio(const fmrb_dbg_req_t *req) {
    if (req->have_on) {
        fmrb_host_set_audio_mute(req->on != 0);
    }
    if (req->have_volume) {
        fmrb_host_set_audio_volume(req->volume);
    }
    fmrb_dbg_writer_t w;
    fmrb_dbg_writer_init(&w);
    fmrb_dbg_resp_begin(&w, req->seq, FMRB_OK);
    msgpack_pack_map(&w.pk, 2);
    fmrb_dbg_pack_kv_bool(&w.pk, "muted", fmrb_host_audio_muted());
    fmrb_dbg_pack_kv_int(&w.pk, "volume", fmrb_host_audio_volume());
    send_writer(&w);
    fmrb_dbg_writer_destroy(&w);
}

// gc {pid?, max?}: a full collection in each app (or in pid alone), run by
// the app's own task at its next message wait -- never from here, which would
// touch a VM another task is running -- with the engine's own collector
// (fmrb_app_gc_request). Waits until every app asked has collected or max ms
// (default 3000) have passed, then answers {apps: [{pid, name, status}]} with
// status "done", "timeout", "skipped" (not running, an engine that does not
// answer, or parked by the debugger) or "gone" (ended meanwhile). For tools
// that read the VM pools (rake regress:run) and want them free of garbage.
#define DEBUGD_GC_WAIT_MS_DEFAULT 3000
#define DEBUGD_GC_POLL_MS         10

static const char *gc_status_str(int st) {
    switch (st) {
        case FMRB_APP_GC_DONE:    return "done";
        case FMRB_APP_GC_PENDING: return "timeout";
        case FMRB_APP_GC_GONE:    return "gone";
        default:                  return "skipped";
    }
}

static void handle_gc(const fmrb_dbg_req_t *req) {
    fmrb_app_info_t list[FMRB_MAX_APPS];
    int32_t n = fmrb_app_ps(list, FMRB_MAX_APPS);
    if (n < 0) n = 0;
    int st[FMRB_MAX_APPS];
    uint32_t gen[FMRB_MAX_APPS];
    uint32_t seq[FMRB_MAX_APPS];
    int32_t m = 0;
    for (int32_t i = 0; i < n; i++) {
        if (req->have_pid && list[i].app_id != req->pid) continue;
        list[m] = list[i];
        if (fmrb_debug_ctx_is_attached(list[m].app_id)) {
            st[m] = FMRB_APP_GC_SKIPPED;
        } else {
            st[m] = fmrb_app_gc_request(list[m].app_id, &gen[m], &seq[m]);
        }
        m++;
    }
    if (req->have_pid && m == 0) { reply_ok(req, FMRB_ERR_INVALID_PARAM); return; }

    int wait_ms = req->max > 0 ? req->max : DEBUGD_GC_WAIT_MS_DEFAULT;
    fmrb_time_t deadline = fmrb_hal_time_get_us() + (fmrb_time_t)wait_ms * 1000;
    for (;;) {
        bool pending = false;
        for (int32_t i = 0; i < m; i++) {
            if (st[i] != FMRB_APP_GC_PENDING) continue;
            st[i] = fmrb_app_gc_poll(list[i].app_id, gen[i], seq[i]);
            if (st[i] == FMRB_APP_GC_PENDING) pending = true;
        }
        if (!pending || fmrb_hal_time_get_us() >= deadline) break;
        fmrb_task_delay_ms(DEBUGD_GC_POLL_MS);
    }

    fmrb_dbg_writer_t w;
    fmrb_dbg_writer_init(&w);
    fmrb_dbg_resp_begin(&w, req->seq, FMRB_OK);
    msgpack_pack_map(&w.pk, 1);
    fmrb_dbg_pack_key(&w.pk, "apps");
    msgpack_pack_array(&w.pk, m);
    for (int32_t i = 0; i < m; i++) {
        msgpack_pack_map(&w.pk, 3);
        fmrb_dbg_pack_kv_int(&w.pk, "pid", list[i].app_id);
        fmrb_dbg_pack_kv_str(&w.pk, "name", list[i].app_name);
        fmrb_dbg_pack_kv_str(&w.pk, "status", gc_status_str(st[i]));
    }
    send_writer(&w);
    fmrb_dbg_writer_destroy(&w);
}

#ifdef FMRB_HAVE_CHECKPOINT
// checkpoint {pid, restart?, max?, hold?}: ask the kernel to have the app write
// its checkpoint image (doc/checkpoint). The kernel holds the steps (it sends
// the app the request, and with restart starts the image again once the app
// has closed); the app writes the image in its own task at a safe point.
// Waits until the app has answered or max ms (default 5000) have passed, then
// answers {status, path | reason}: status "written" (path: the image),
// "refused" (reason), or "timeout" (the app did not answer: it may be busy
// in a long on_update, or suspended).
//
// With hold (ms, a move: F2, D15) the app does not close once written: it is
// held where the image was taken, and the answer is "held" {path, size}. Then
// checkpoint_finish {pid, close, reason?} closes it (the image went on
// elsewhere) or lets it go on; nobody telling it within hold lets it go on.
#define DEBUGD_CKPT_WAIT_MS_DEFAULT 5000
#define DEBUGD_FILE_PIECE 3072   // file_get: bytes per answer (a frame is 4 KB)

static fmrb_app_ckpt_status_t wait_ckpt(int pid, int max, char *what, size_t what_len,
                                        bool (*settled)(fmrb_app_ckpt_status_t))
{
    int wait_ms = max > 0 ? max : DEBUGD_CKPT_WAIT_MS_DEFAULT;
    fmrb_time_t deadline = fmrb_hal_time_get_us() + (fmrb_time_t)wait_ms * 1000;
    fmrb_app_ckpt_status_t st;
    for (;;) {
        st = fmrb_app_checkpoint_poll(pid, what, what_len);
        if (settled(st) || fmrb_hal_time_get_us() >= deadline) break;
        fmrb_task_delay_ms(DEBUGD_GC_POLL_MS);
    }
    return st;
}

static bool ckpt_answered(fmrb_app_ckpt_status_t st) { return st != FMRB_APP_CKPT_PENDING; }

static bool ckpt_finished(fmrb_app_ckpt_status_t st)
{
    return st != FMRB_APP_CKPT_PENDING && st != FMRB_APP_CKPT_HELD;
}

static const char *ckpt_status_str(fmrb_app_ckpt_status_t st)
{
    switch (st) {
        case FMRB_APP_CKPT_WRITTEN:   return "written";
        case FMRB_APP_CKPT_REFUSED:   return "refused";
        case FMRB_APP_CKPT_HELD:      return "held";
        case FMRB_APP_CKPT_CLOSED:    return "closed";
        case FMRB_APP_CKPT_CONTINUED: return "continued";
        case FMRB_APP_CKPT_NONE:      return "none";
        default:                      return "timeout";
    }
}

// The size of a file under /tmp/ (0 when it cannot be read).
static uint32_t file_size_of(const char *path)
{
    uint8_t b;
    size_t got = 0;
    uint32_t size = 0;
    fmrb_app_transfer_read(path, 0, &b, 0, &got, &size);
    return size;
}

static void reply_ckpt(const fmrb_dbg_req_t *req, fmrb_app_ckpt_status_t st, const char *what)
{
    bool has_path = st == FMRB_APP_CKPT_WRITTEN || st == FMRB_APP_CKPT_HELD || st == FMRB_APP_CKPT_CLOSED;
    bool has_reason = st == FMRB_APP_CKPT_REFUSED || st == FMRB_APP_CKPT_CONTINUED;
    fmrb_dbg_writer_t w;
    fmrb_dbg_writer_init(&w);
    fmrb_dbg_resp_begin(&w, req->seq, FMRB_OK);
    msgpack_pack_map(&w.pk, 1 + (has_path || has_reason ? 1 : 0) + (st == FMRB_APP_CKPT_HELD ? 1 : 0));
    fmrb_dbg_pack_kv_str(&w.pk, "status", ckpt_status_str(st));
    if (has_path) fmrb_dbg_pack_kv_str(&w.pk, "path", what);
    if (has_reason) fmrb_dbg_pack_kv_str(&w.pk, "reason", what);
    if (st == FMRB_APP_CKPT_HELD) fmrb_dbg_pack_kv_int(&w.pk, "size", file_size_of(what));
    send_writer(&w);
    fmrb_dbg_writer_destroy(&w);
}

static void handle_checkpoint(const fmrb_dbg_req_t *req) {
    if (!req->have_pid) { reply_ok(req, FMRB_ERR_INVALID_PARAM); return; }
    fmrb_err_t err = fmrb_app_checkpoint_ask(req->pid, req->restart, req->hold);
    if (err != FMRB_OK) { reply_ok(req, err); return; }
    char what[256];
    fmrb_app_ckpt_status_t st = wait_ckpt(req->pid, req->max, what, sizeof(what), ckpt_answered);
    reply_ckpt(req, st, what);
}

// checkpoint_finish {pid, close, reason?, max?}: a held app closes (close) or
// goes on, through the kernel. Answers {status: "closed" | "continued" (with
// the reason it went on: also when the hold had run out before) | "timeout"}.
static void handle_checkpoint_finish(const fmrb_dbg_req_t *req) {
    if (!req->have_pid) { reply_ok(req, FMRB_ERR_INVALID_PARAM); return; }
    char what[256];
    fmrb_app_ckpt_status_t st = fmrb_app_checkpoint_poll(req->pid, what, sizeof(what));
    if (st == FMRB_APP_CKPT_HELD) {
        fmrb_err_t err = fmrb_app_checkpoint_finish(req->pid, req->close, req->reason);
        if (err != FMRB_OK) { reply_ok(req, err); return; }
        st = wait_ckpt(req->pid, req->max, what, sizeof(what), ckpt_finished);
    }
    reply_ckpt(req, st, what);
}

// move {pid, to, hold?, max?}: move the app to the machine whose Asterism node
// ID is `to` (doc/checkpoint F5). The kernel holds the steps (it asks that
// machine first, has the app write its image and hold, has the transfer app
// carry it, and closes it here once it is up there). Waits until it is over or
// max ms (default 40000: the transfer app may have to connect first) and
// answers {status: "moved", detail} (where it went on: "moved to <node> as PID
// n[; it lacks: names]"), {status: "stayed", reason} (refused or not
// answered, before or after it was held: the app goes on here) or {status:
// "timeout"}.
#define DEBUGD_MOVE_WAIT_MS_DEFAULT 40000

static void handle_move(const fmrb_dbg_req_t *req) {
    if (!req->have_pid) { reply_ok(req, FMRB_ERR_INVALID_PARAM); return; }
    fmrb_err_t err = fmrb_app_checkpoint_move(req->pid, req->to, req->hold);
    if (err != FMRB_OK) { reply_ok(req, err); return; }
    char what[256];
    fmrb_app_ckpt_status_t st = wait_ckpt(req->pid, req->max > 0 ? req->max : DEBUGD_MOVE_WAIT_MS_DEFAULT,
                                          what, sizeof(what), ckpt_finished);
    fmrb_dbg_writer_t w;
    fmrb_dbg_writer_init(&w);
    fmrb_dbg_resp_begin(&w, req->seq, FMRB_OK);
    if (st == FMRB_APP_CKPT_CLOSED) {
        msgpack_pack_map(&w.pk, 2);
        fmrb_dbg_pack_kv_str(&w.pk, "status", "moved");
        fmrb_dbg_pack_kv_str(&w.pk, "detail", what);
    } else if (st == FMRB_APP_CKPT_REFUSED || st == FMRB_APP_CKPT_CONTINUED) {
        msgpack_pack_map(&w.pk, 3);
        fmrb_dbg_pack_kv_str(&w.pk, "status", "stayed");
        fmrb_dbg_pack_kv_str(&w.pk, "reason", what);
        fmrb_dbg_pack_kv_str(&w.pk, "here", st == FMRB_APP_CKPT_REFUSED ? "not stopped" : "went on");
    } else {
        msgpack_pack_map(&w.pk, 1);
        fmrb_dbg_pack_kv_str(&w.pk, "status", "timeout");
    }
    send_writer(&w);
    fmrb_dbg_writer_destroy(&w);
}

// restore {path, max?}: the kernel starts the image (a .ckpt under /tmp/);
// answers once it is restored, before it goes on: {status: "restored", pid},
// {status: "refused", reason} (it could not be started or restored: a damaged
// image, one of another build, no room, a method its .app.toml requires that
// this build lacks) or {status: "timeout"}. A restored image whose program
// calls methods this build has none of adds {missing: [names]} (it goes on).
static void handle_restore(const fmrb_dbg_req_t *req) {
    fmrb_err_t err = fmrb_app_checkpoint_restore_ask(req->path);
    if (err != FMRB_OK) { reply_ok(req, err); return; }
    int wait_ms = req->max > 0 ? req->max : DEBUGD_CKPT_WAIT_MS_DEFAULT;
    fmrb_time_t deadline = fmrb_hal_time_get_us() + (fmrb_time_t)wait_ms * 1000;
    char what[256];
    int32_t pid = -1;
    fmrb_app_restore_status_t st;
    for (;;) {
        st = fmrb_app_checkpoint_restore_poll(&pid, what, sizeof(what));
        if (st != FMRB_APP_RESTORE_PENDING || fmrb_hal_time_get_us() >= deadline) break;
        fmrb_task_delay_ms(DEBUGD_GC_POLL_MS);
    }
    fmrb_dbg_writer_t w;
    fmrb_dbg_writer_init(&w);
    fmrb_dbg_resp_begin(&w, req->seq, FMRB_OK);
    if (st == FMRB_APP_RESTORE_DONE) {
        // what: the names it lacks, one space between them
        int nmiss = 0;
        for (const char *p = what; *p; ) {
            while (*p == ' ') p++;
            if (!*p) break;
            nmiss++;
            p += strcspn(p, " ");
        }
        msgpack_pack_map(&w.pk, nmiss > 0 ? 3 : 2);
        fmrb_dbg_pack_kv_str(&w.pk, "status", "restored");
        fmrb_dbg_pack_kv_int(&w.pk, "pid", pid);
        if (nmiss > 0) {
            fmrb_dbg_pack_key(&w.pk, "missing");
            msgpack_pack_array(&w.pk, nmiss);
            for (const char *p = what; *p; ) {
                while (*p == ' ') p++;
                if (!*p) break;
                size_t n = strcspn(p, " ");
                msgpack_pack_str(&w.pk, n);
                msgpack_pack_str_body(&w.pk, p, n);
                p += n;
            }
        }
    } else if (st == FMRB_APP_RESTORE_REFUSED) {
        msgpack_pack_map(&w.pk, 2);
        fmrb_dbg_pack_kv_str(&w.pk, "status", "refused");
        fmrb_dbg_pack_kv_str(&w.pk, "reason", what);
    } else {
        msgpack_pack_map(&w.pk, 1);
        fmrb_dbg_pack_kv_str(&w.pk, "status", "timeout");
    }
    send_writer(&w);
    fmrb_dbg_writer_destroy(&w);
}

// file_get {path, pos}: up to 3 KB of a file under /tmp/ at pos, as
// {data (bin), size (the file's)}. The pieces of an image going out.
static void handle_file_get(const fmrb_dbg_req_t *req) {
    // the debugd task only; external RAM on a device (internal RAM is the scarce one)
    FMRB_EXT_RAM_BSS_ATTR static uint8_t piece[DEBUGD_FILE_PIECE];
    size_t got = 0;
    uint32_t size = 0;
    fmrb_err_t err = fmrb_app_transfer_read(req->path, req->pos, piece, sizeof(piece), &got, &size);
    if (err != FMRB_OK) { reply_ok(req, err); return; }
    fmrb_dbg_writer_t w;
    fmrb_dbg_writer_init(&w);
    fmrb_dbg_resp_begin(&w, req->seq, FMRB_OK);
    msgpack_pack_map(&w.pk, 2);
    fmrb_dbg_pack_key(&w.pk, "data");
    msgpack_pack_bin(&w.pk, got);
    msgpack_pack_bin_body(&w.pk, piece, got);
    fmrb_dbg_pack_kv_int(&w.pk, "size", size);
    send_writer(&w);
    fmrb_dbg_writer_destroy(&w);
}

// file_begin {path, size} -> {id}; file_chunk {id, pos, data}; file_commit
// {id}: a file under /tmp/ coming in pieces, which appears at its path only
// at the commit (fmrb_app_transfer.h).
static void handle_file_begin(const fmrb_dbg_req_t *req) {
    uint32_t id = 0;
    fmrb_err_t err = fmrb_app_transfer_begin(req->path, req->size, &id);
    if (err != FMRB_OK) { reply_ok(req, err); return; }
    fmrb_dbg_writer_t w;
    fmrb_dbg_writer_init(&w);
    fmrb_dbg_resp_begin(&w, req->seq, FMRB_OK);
    msgpack_pack_map(&w.pk, 1);
    fmrb_dbg_pack_kv_int(&w.pk, "id", id);
    send_writer(&w);
    fmrb_dbg_writer_destroy(&w);
}

static void handle_file_chunk(const fmrb_dbg_req_t *req) {
    reply_ok(req, fmrb_app_transfer_chunk(req->id, req->pos, req->data, req->data_len));
}

static void handle_file_commit(const fmrb_dbg_req_t *req) {
    reply_ok(req, fmrb_app_transfer_commit(req->id));
}
#endif

// --- dispatch --------------------------------------------------------------

// Hook-based commands drive the shared debug core (attach/park/inspect). They
// are refused while the on-device gem owns the session; the non-hook commands
// (version/ps/log_read/spawn/app-ctl/audio/gc) stay available to remote
// clients.
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
        case DBG_CMD_AUDIO:    handle_audio(req);    break;
        case DBG_CMD_GC:       handle_gc(req);       break;
#ifdef FMRB_HAVE_CHECKPOINT
        case DBG_CMD_CHECKPOINT: handle_checkpoint(req); break;
        case DBG_CMD_CHECKPOINT_FINISH: handle_checkpoint_finish(req); break;
        case DBG_CMD_RESTORE:     handle_restore(req);     break;
        case DBG_CMD_FILE_GET:    handle_file_get(req);    break;
        case DBG_CMD_FILE_BEGIN:  handle_file_begin(req);  break;
        case DBG_CMD_FILE_CHUNK:  handle_file_chunk(req);  break;
        case DBG_CMD_FILE_COMMIT: handle_file_commit(req); break;
        case DBG_CMD_MOVE:        handle_move(req);        break;
#endif

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

#ifdef CONFIG_IDF_TARGET_LINUX
// Whether debugd runs, and where its TCP transport listens: system_conf's
// debugd / debugd_bind. It starts and kills apps for anyone who can reach the
// port, with no authentication, so the installed native package (which runs
// as root on the Cardputer) has it off by default and, when turned on, keeps
// it on the loopback (reach it from elsewhere through an ssh forward). The
// simulation and the development native build keep listening on every
// interface (the simulation is driven from outside its container).
#if defined(FMRB_NATIVE_PACKAGE) && FMRB_NATIVE_PACKAGE
#define DEBUGD_DEFAULT_ON      false
#define DEBUGD_DEFAULT_BIND    "127.0.0.1"
#define DEBUGD_LOOPBACK_ONLY   1
#else
#define DEBUGD_DEFAULT_ON      true
#define DEBUGD_DEFAULT_BIND    "0.0.0.0"
#define DEBUGD_LOOPBACK_ONLY   0
#endif

static const char k_default_bind[] = DEBUGD_DEFAULT_BIND;

static bool debugd_conf_enabled(void) {
    char errbuf[64];
    toml_table_t *conf = fmrb_toml_load_file("/etc/system_conf.toml", errbuf, sizeof(errbuf));
    if (!conf) {
        conf = fmrb_toml_load_file("/etc/system_conf.factory.toml", errbuf, sizeof(errbuf));
    }
    bool on = DEBUGD_DEFAULT_ON;
    const char *bind = k_default_bind;
    if (conf) {
        on = fmrb_toml_get_bool(conf, "debugd", DEBUGD_DEFAULT_ON);
        bind = fmrb_toml_get_string(conf, "debugd_bind", k_default_bind);
    }
    if (bind == NULL || bind[0] == '\0') {
        if (bind != NULL && bind != k_default_bind) fmrb_sys_free((void *)bind);
        bind = k_default_bind;
    }
#if DEBUGD_LOOPBACK_ONLY
    if (strcmp(bind, "127.0.0.1") != 0) {
        FMRB_LOGW(TAG, "debugd_bind \"%s\" ignored: this build listens on 127.0.0.1 only", bind);
        if (bind != k_default_bind) fmrb_sys_free((void *)bind);
        bind = k_default_bind;
    }
#endif
    if (on && fmrb_debug_transport_tcp_set_bind(bind) != FMRB_OK) {
        FMRB_LOGE(TAG, "debugd_bind \"%s\" is not an IPv4 address; debugd not started", bind);
        on = false;
    }
    if (bind != k_default_bind) fmrb_sys_free((void *)bind);
    if (conf) toml_free(conf);
    if (!on) {
        FMRB_LOGI(TAG, "debugd off (system_conf debugd = false)");
    }
    return on;
}
#endif

void fmrb_debugd_init(void) {
    if (s_started) return;
    s_started = true;
#ifdef CONFIG_IDF_TARGET_LINUX
    if (!debugd_conf_enabled()) return;
#endif
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
