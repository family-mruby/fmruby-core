// Moving an mruby app with mruby-checkpoint (doc/checkpoint, F1).
//
// The way through:
//   1. A tool asks the debug server ("checkpoint"), or an app asks
//      (FmrbApp.checkpoint: the shell's `checkpoint`, an app asking
//      for its own image); either asks the kernel the same way (ask_kernel ->
//      APP_CONTROL "checkpoint" to the kernel). An app is answered with
//      "checkpoint_result" by the kernel.
//   2. The kernel (Ruby, both engines) sends the app APP_CONTROL "checkpoint".
//   3. The app's _spin (picoruby-fmrb-app) takes it in its own task:
//      fmrb_app_checkpoint_request raises the gem's request and ends the wait.
//      Inside _spin the VM is in C and no image can be taken (D5); once _spin
//      has returned to main_loop, the next instruction boundary is a safe
//      point and the gem writes the image there.
//   4. The gem hands the image to ckpt_written (below): it is written to
//      /tmp/<name>.ckpt with a .toml beside it (the attributes the spawner
//      reads, as for any app), the app's on_checkpoint runs, every Task of the
//      VM is ended and the app task closes the VM the ordinary way. The kernel
//      is told "checkpoint_done"; it starts the image again once the app has
//      gone, when it was asked to (D15 for F1).
//   5. Spawning a .ckpt (fmrb_app_spawner.c) opens a VM, sets the gem up the
//      same way, restores the image, calls on_restore and continues it
//      (fmrb_app_checkpoint_resume).
//
// A move to another program (F2, D15) asks with a hold: in 4 the app does not
// close but stops where the image was taken (in ckpt_written, so neither the
// program nor its Tasks run a step beyond the image) and tells the kernel
// "checkpoint_held". The image is carried to the other program and started
// there (fmrb_app_checkpoint_restore_ask); the kernel then tells the app
// "checkpoint_finish" {close}: it closes as in 4 when the image goes on
// elsewhere, and goes on from where it stopped when it does not. The kernel
// lets it go on when nobody has told it within the hold; the app itself does
// so a little later still, should the kernel not answer at all.
//
// What goes on with the program beside the VM is in the embedder's section of
// the image (EMB0, written by emb_dump): Machine.board_millis (D9: each VM
// runs at an offset from the board's clock) and the HAL's tick state of the
// running timeslice (D10, task_hal.c). The gem's work areas (the image being
// written, its tables) come from fmrb_sys_malloc, not from the app's pool
// (D11).
//
// Between builds (F4), the image names the methods the program calls (the
// gem's SND0). The arriving side restores it all the same and tells the names
// its build lacks (the log, the restore answer's "missing"): a movable app
// guards the calls that are not common to every build. It refuses only the
// names the app declares in its .app.toml as requires = [...], carried in the
// image's .toml (doc/checkpoint/movable_apps.md).
//
// Per-app state is a small table indexed by app id, in this file only, so the
// builds without the gem carry none of it.

#ifdef FMRB_HAVE_CHECKPOINT

#include <stdio.h>
#include <string.h>
#include <mruby.h>
#include <mruby/variable.h>
#include <mruby/string.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/error.h>
#include <mruby/throw.h>
#include <mruby/checkpoint.h>
#include <task.h>
#include <msgpack.h>

#include "fmrb_app_checkpoint.h"
#include "fmrb_app.h"
#include "fmrb_attr.h"
#include "fmrb_hal.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_msg.h"
#include "fmrb_task_config.h"
#include "fmrb_gfx.h"
#include "fmrb_gfx_cmd.h"
#include "fmrb_theme.h"
#include "fmrb_toml.h"

static const char *TAG = "ckpt";

// The C data of picoruby-fmrb-app the image carries (gfx.c: FmrbGfx, F3).
// Registered here, in a VM that opens a checkpoint only (D7).
extern void fmrb_gfx_checkpoint_register(mrb_state *mrb);

// The HAL's tick state of one VM (task_hal.c, FMRB_HAVE_CHECKPOINT).
extern int mrb_hal_task_slice_get(mrb_state *mrb, uint32_t *countdown, uint32_t *pending,
                                  uint8_t *switch_pending, uint32_t *sub_tick_us);
extern int mrb_hal_task_slice_set(mrb_state *mrb, uint32_t countdown, uint32_t pending,
                                  uint8_t switch_pending, uint32_t sub_tick_us);
// The clock Machine.board_millis reads (picoruby-machine).
extern uint64_t Machine_uptime_us(void);

// Where the image of an app goes (D11: /tmp).
#define CKPT_DIR "/tmp/"
#define CKPT_WHAT_MAX 256
// The method names an app declares it cannot go on without (requires = [...]
// in its .app.toml), one space between them.
#define CKPT_REQUIRES_MAX 256

// The app object, kept in the VM under a name Ruby cannot spell while a
// request waits, so that the image carries it and both sides find it.
#define APP_GV "_fmrb_checkpoint_app_"

// The embedder's section (EMB0): its own version byte first.
#define EMB_VERSION 1

// A held app goes on by itself this long after the hold the kernel was given,
// should the kernel never tell it (the kernel's own bound comes first).
#define HOLD_GRACE_MS 2000
// Messages that arrive while the app is held are given back to it when it goes
// on; this many at most (a headless app gets few).
#define HOLD_STASH_MAX 16
#define HOLD_POLL_MS 50

typedef struct {
    mrb_state *mrb;                 // the VM this entry is for (NULL: none)
    int64_t millis_offset;          // board_millis = board clock + this (D9)
    char origin[FMRB_MAX_PATH_LEN]; // the .rb the program was first compiled from
    volatile int status;            // fmrb_app_ckpt_status_t
    char what[CKPT_WHAT_MAX];       // the image's path, or why it was refused
    int32_t hold_ms;                // the request's hold (0: close once written)
    char requires[CKPT_REQUIRES_MAX]; // requires = [...] of its .app.toml (or the image's .toml)
} ckpt_app_t;

// External RAM on a device: kilobytes, and only the app tasks touch them.
FMRB_EXT_RAM_BSS_ATTR static ckpt_app_t s_ckpt[FMRB_MAX_APPS];

// The last image asked to be started (fmrb_app_checkpoint_restore_ask): one
// at a time, as the kernel moves one app at a time.
typedef struct {
    volatile int status;            // fmrb_app_restore_status_t
    int32_t pid;
    char path[FMRB_MAX_PATH_LEN];
    char what[CKPT_WHAT_MAX];       // why it was refused, or (restored) the names it lacks
} ckpt_restore_t;

FMRB_EXT_RAM_BSS_ATTR static ckpt_restore_t s_restore;

static void put_u32(uint8_t *p, uint32_t v);
static bool restore_answer(const char *path, int status, int32_t pid, const char *what);

static ckpt_app_t *entry_of(const fmrb_app_task_context_t *ctx)
{
    if (!ctx || ctx->app_id < 0 || ctx->app_id >= FMRB_MAX_APPS) return NULL;
    return &s_ckpt[ctx->app_id];
}

static ckpt_app_t *entry_of_vm(mrb_state *mrb)
{
    ckpt_app_t *e = entry_of(fmrb_current());
    return (e && e->mrb == mrb) ? e : NULL;
}

// ---- telling the kernel ------------------------------------------------------

// A msgpack str (fixstr or str8) into buf; returns the bytes written.
static size_t pack_str(uint8_t *buf, size_t room, const char *s)
{
    size_t n = strlen(s);
    if (n > 255) n = 255;
    size_t head = n < 32 ? 1 : 2;
    if (head + n > room) n = room > head ? room - head : 0;
    if (n < 32) {
        buf[0] = (uint8_t)(0xA0 | n);
    } else {
        buf[0] = 0xD9;
        buf[1] = (uint8_t)n;
    }
    memcpy(buf + head, s, n);
    return head + n;
}

// A small msgpack map built in an APP_CONTROL message.
typedef struct {
    fmrb_msg_t msg;
    size_t pos;
} ctl_msg_t;

static void ctl_begin(ctl_msg_t *m, fmrb_proc_id_t src, unsigned pairs)
{
    memset(&m->msg, 0, sizeof(m->msg));
    m->msg.type = FMRB_MSG_TYPE_APP_CONTROL;
    m->msg.src_pid = src;
    m->msg.data[0] = (uint8_t)(0x80 | pairs);
    m->pos = 1;
}

static void ctl_str(ctl_msg_t *m, const char *s)
{
    m->pos += pack_str(m->msg.data + m->pos, sizeof(m->msg.data) - m->pos, s);
}

static void ctl_int(ctl_msg_t *m, uint32_t v)
{
    if (m->pos + 5 > sizeof(m->msg.data)) return;
    m->msg.data[m->pos++] = 0xCE;
    put_u32(m->msg.data + m->pos, v);
    m->pos += 4;
}

static void ctl_bool(ctl_msg_t *m, bool v)
{
    if (m->pos < sizeof(m->msg.data)) m->msg.data[m->pos++] = v ? 0xC3 : 0xC2;
}

static fmrb_err_t ctl_send(ctl_msg_t *m, fmrb_proc_id_t dest)
{
    m->msg.size = (uint32_t)m->pos;
    return fmrb_msg_send(dest, &m->msg, 100);
}

// {"cmd" => cmd, key => value} (key NULL: {"cmd" => cmd} alone)
static void send_kernel(fmrb_proc_id_t src, const char *cmd, const char *key, const char *value)
{
    ctl_msg_t m;
    ctl_begin(&m, src, key ? 2 : 1);
    ctl_str(&m, "cmd");
    ctl_str(&m, cmd);
    if (key) {
        ctl_str(&m, key);
        ctl_str(&m, value);
    }
    if (ctl_send(&m, PROC_ID_KERNEL) != FMRB_OK) {
        FMRB_LOGW(TAG, "could not tell the kernel %s", cmd);
    }
}

static void refuse(fmrb_app_task_context_t *ctx, const char *why)
{
    ckpt_app_t *e = entry_of(ctx);
    FMRB_LOGW(TAG, "[%s] checkpoint refused: %s", ctx->app_name, why);
    if (e) {
        snprintf(e->what, sizeof(e->what), "%s", why);
        e->status = FMRB_APP_CKPT_REFUSED;
    }
    send_kernel(ctx->app_id, "checkpoint_refused", "reason", why);
}

// ---- the gem's work areas (D11) ----------------------------------------------

static void *work_realloc(void *ud, void *p, size_t size)
{
    (void)ud;
    if (size == 0) {
        fmrb_sys_free(p);
        return NULL;
    }
    return fmrb_sys_realloc(p, size);
}

// ---- the embedder's section: the clock and the tick state (D9, D10) ----------

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int64_t board_millis_of(ckpt_app_t *e)
{
    return (int64_t)(Machine_uptime_us() / 1000) + (e ? e->millis_offset : 0);
}

// version u8, board_millis i64, tick countdown u32, pending ticks u32,
// switch pending u8, the part of a tick gone by (us) u32, origin (u16 + bytes)
static int emb_dump(mrb_state *mrb, mrb_checkpoint_buf *out, void *ud)
{
    (void)ud;
    ckpt_app_t *e = entry_of_vm(mrb);
    uint32_t countdown = 0, pending = 0, sub_us = 0;
    uint8_t switch_pending = 0;
    if (mrb_hal_task_slice_get(mrb, &countdown, &pending, &switch_pending, &sub_us) != 0) {
        return -1;
    }
    uint8_t b[1 + 8 + 4 + 4 + 1 + 4 + 2];
    int64_t ms = board_millis_of(e);
    size_t n = 0;
    b[n++] = EMB_VERSION;
    put_u32(b + n, (uint32_t)((uint64_t)ms >> 32)); n += 4;
    put_u32(b + n, (uint32_t)ms); n += 4;
    put_u32(b + n, countdown); n += 4;
    put_u32(b + n, pending); n += 4;
    b[n++] = switch_pending;
    put_u32(b + n, sub_us); n += 4;
    const char *origin = e ? e->origin : "";
    size_t olen = strlen(origin);
    b[n++] = (uint8_t)(olen >> 8);
    b[n++] = (uint8_t)olen;
    mrb_checkpoint_buf_bytes(mrb, out, b, n);
    mrb_checkpoint_buf_bytes(mrb, out, origin, olen);
    return 0;
}

static void emb_restore(mrb_state *mrb, const uint8_t *p, size_t len, void *ud)
{
    (void)ud;
    ckpt_app_t *e = entry_of_vm(mrb);
    if (len < 28 || p[0] != EMB_VERSION) {
        FMRB_LOGW(TAG, "the image's embedder section is not one this build reads (%u bytes)", (unsigned)len);
        return;
    }
    int64_t ms = (int64_t)(((uint64_t)get_u32(p + 1) << 32) | get_u32(p + 5));
    uint32_t countdown = get_u32(p + 9), pending = get_u32(p + 13);
    uint8_t switch_pending = p[17];
    uint32_t sub_us = get_u32(p + 18);
    size_t olen = ((size_t)p[22] << 8) | p[23];
    if (24 + olen > len) olen = len - 24;
    if (e) {
        e->millis_offset = ms - (int64_t)(Machine_uptime_us() / 1000);
        size_t n = olen < sizeof(e->origin) - 1 ? olen : sizeof(e->origin) - 1;
        memcpy(e->origin, p + 24, n);
        e->origin[n] = '\0';
    }
    mrb_hal_task_slice_set(mrb, countdown, pending, switch_pending, sub_us);
    FMRB_LOGI(TAG, "restored clock: board_millis goes on from %lld (offset %lld), slice countdown %u, "
              "pending ticks %u%s", (long long)ms, e ? (long long)e->millis_offset : 0LL,
              (unsigned)countdown, (unsigned)pending, switch_pending ? ", switch pending" : "");
}

int64_t fmrb_app_checkpoint_millis_offset(mrb_state *mrb)
{
    ckpt_app_t *e = entry_of_vm(mrb);
    return e ? e->millis_offset : 0;
}

// ---- writing ------------------------------------------------------------------

// "/app/demo/counter.app.rb" -> "counter"
static void stem_of(const char *path, char *out, size_t n)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t k = 0;
    while (base[k] && base[k] != '.' && k < n - 1) {
        out[k] = base[k];
        k++;
    }
    out[k] = '\0';
    if (k == 0) snprintf(out, n, "app");
}

static fmrb_err_t write_file(const char *path, const void *buf, size_t len)
{
    fmrb_file_t f = NULL;
    fmrb_err_t ret = fmrb_hal_file_open(path, FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_TRUNC, &f);
    if (ret != FMRB_OK) return ret;
    size_t done = 0;
    ret = fmrb_hal_file_write(f, buf, len, &done);
    fmrb_err_t cret = fmrb_hal_file_close(f);
    if (ret == FMRB_OK && done != len) ret = FMRB_ERR_FAILED;
    return ret != FMRB_OK ? ret : cret;
}

// The attributes a spawn of the image reads (the same names as an .app.toml):
// what the app was started with, from its context. large_memory is what the
// app asked for, or set when it ran in a large pool and has used more than an
// ordinary pool holds: on the S3 an ordinary app lands in a large pool when
// the ordinary one is busy, and only then could it have outgrown one. Saying
// large for every app that merely ran in a large pool made the destination
// refuse it whenever its own large pool was taken (the transfer app holds one).
static size_t pool_peak(const fmrb_app_task_context_t *ctx);

static bool image_needs_large(const fmrb_app_task_context_t *ctx)
{
    if (ctx->large_memory) return true;
    if (!fmrb_mempool_is_user_large(ctx->mempool_id)) return false;
    return pool_peak(ctx) > fmrb_get_mempool_size(POOL_ID_USER_APP0);
}

static fmrb_err_t write_attributes(const fmrb_app_task_context_t *ctx, const char *path)
{
    const int cap = 768;
    char *t = (char *)fmrb_sys_malloc(cap);
    if (!t) return FMRB_ERR_NO_MEMORY;
    int n = snprintf(t, cap,
                     "# Written with the checkpoint image beside it (doc/checkpoint).\n"
                     "app_screen_name = \"%s\"\n"
                     "movable = true\n"
                     "large_memory = %d\n",
                     ctx->app_name, image_needs_large(ctx) ? 1 : 0);
    if (ctx->headless) {
        n += snprintf(t + n, cap - n, "default_window_mode = \"background\"\n");
    } else {
        // The window as it is now (moved, resized, switched to fullscreen):
        // the image goes on in a window of the same place, size and kind.
        if (ctx->fullscreen) {
            n += snprintf(t + n, cap - n, "default_window_mode = \"fullscreen\"\n");
        } else {
            n += snprintf(t + n, cap - n,
                          "default_window_mode = \"window\"\n"
                          "default_window_width = %u\ndefault_window_height = %u\n"
                          "default_window_pos_x = %u\ndefault_window_pos_y = %u\n",
                          ctx->window_width, ctx->window_height, ctx->window_pos_x,
                          ctx->window_pos_y);
        }
        n += snprintf(t + n, cap - n,
                      "resizable = %d\nrounded_corners = %d\nfullscreen_switchable = %d\n"
                      "fullscreen_hires = %d\nmin_window_width = %u\nmin_window_height = %u\n",
                      ctx->resizable ? 1 : 0, ctx->rounded_corners ? 1 : 0,
                      ctx->fullscreen_switchable ? 1 : 0, ctx->fullscreen_hires ? 1 : 0,
                      ctx->min_window_width, ctx->min_window_height);
    }
    const ckpt_app_t *e = entry_of(ctx);
    if (e && e->requires[0]) {
        // requires = ["a", "b"]: the names, one space between them, quoted
        n += snprintf(t + n, cap - n, "requires = [");
        const char *p = e->requires;
        bool first = true;
        while (*p && n < cap - 8) {
            size_t len = strcspn(p, " ");
            n += snprintf(t + n, cap - n, "%s\"%.*s\"", first ? "" : ", ", (int)len, p);
            first = false;
            p += len;
            while (*p == ' ') p++;
        }
        n += snprintf(t + n, cap - n, "]\n");
    }
    if (n >= cap) n = cap - 1;
    fmrb_err_t ret = write_file(path, t, (size_t)n);
    fmrb_sys_free(t);
    return ret;
}

// requires = ["pack", ...] of the attributes beside path (an .app.rb's
// .app.toml, an image's .toml): the methods the app declares it cannot go on
// without, one space between them in out ("" when it declares none).
static void read_requires(const char *path, char *out, size_t n)
{
    out[0] = '\0';
    char toml[FMRB_MAX_PATH_LEN];
    snprintf(toml, sizeof(toml), "%s", path);
    char *dot = strrchr(toml, '.');
    if (!dot || (size_t)(dot - toml) + 6 > sizeof(toml)) return;
    strcpy(dot, ".toml");
    char errbuf[64];
    toml_table_t *conf = fmrb_toml_load_file(toml, errbuf, sizeof(errbuf));
    if (!conf) return;
    toml_array_t *arr = toml_array_in(conf, "requires");
    size_t used = 0;
    for (int i = 0; arr && i < toml_array_nelem(arr); i++) {
        toml_datum_t d = toml_string_at(arr, i);
        if (!d.ok) continue;
        size_t len = strlen(d.u.s);
        if (len > 0 && strchr(d.u.s, ' ') == NULL && used + len + 2 <= n) {
            if (used) out[used++] = ' ';
            memcpy(out + used, d.u.s, len);
            used += len;
            out[used] = '\0';
        }
        fmrb_toml_xfree(d.u.s);
    }
    toml_free(conf);
}

// Call an app hook if the app defines it; an exception it raises is logged
// and dropped (it must not take the move down with it).
static void call_hook(mrb_state *mrb, mrb_value app, const char *name)
{
    if (mrb_nil_p(app) || !mrb_respond_to(mrb, app, mrb_intern_cstr(mrb, name))) return;
    struct mrb_jmpbuf *prev = mrb->jmp;
    struct mrb_jmpbuf jmp;
    MRB_TRY(&jmp) {
        mrb->jmp = &jmp;
        mrb_funcall(mrb, app, name, 0);
        mrb->jmp = prev;
    } MRB_CATCH(&jmp) {
        mrb->jmp = prev;
        FMRB_LOGW(TAG, "%s raised; going on", name);
        mrb_print_error(mrb);
        mrb->exc = NULL;
    } MRB_END_EXC(&jmp);
}

// End every Task of the VM: the program is the image's now. The scheduler
// returns to the app task once none is left (the running one stops at the
// end of the safe point: terminating it raises the switch request).
static void end_all_tasks(mrb_state *mrb)
{
    mrb_value tasks[32];
    int n = 0;
    for (int q = 0; q < MRB_NUM_TASK_QUEUE; q++) {
        for (mrb_task *t = mrb->task.queues[q]; t && n < (int)(sizeof(tasks) / sizeof(tasks[0])); t = t->next) {
            if (t->status != MRB_TASK_STATUS_DORMANT) tasks[n++] = t->self;
        }
    }
    for (int i = 0; i < n; i++) mrb_terminate_task(mrb, tasks[i]);
}

// The value of a str key of a msgpack map (NULL when absent or not a str).
static const msgpack_object *map_get(const msgpack_object *map, const char *key)
{
    if (map->type != MSGPACK_OBJECT_MAP) return NULL;
    size_t klen = strlen(key);
    for (uint32_t i = 0; i < map->via.map.size; i++) {
        const msgpack_object *k = &map->via.map.ptr[i].key;
        if (k->type == MSGPACK_OBJECT_STR && k->via.str.size == klen &&
            memcmp(k->via.str.ptr, key, klen) == 0) {
            return &map->via.map.ptr[i].val;
        }
    }
    return NULL;
}

// Is msg the kernel's "checkpoint_finish" {close, reason}? Fills *close and
// why (the reason, when it goes on).
static bool finish_message(const fmrb_msg_t *msg, bool *close, char *why, size_t whylen)
{
    if (msg->type != FMRB_MSG_TYPE_APP_CONTROL) return false;
    msgpack_unpacked up;
    msgpack_unpacked_init(&up);
    size_t off = 0;
    bool found = false;
    if (msgpack_unpack_next(&up, (const char *)msg->data, msg->size, &off) == MSGPACK_UNPACK_SUCCESS) {
        const msgpack_object *cmd = map_get(&up.data, "cmd");
        if (cmd && cmd->type == MSGPACK_OBJECT_STR && cmd->via.str.size == 17 &&
            memcmp(cmd->via.str.ptr, "checkpoint_finish", 17) == 0) {
            const msgpack_object *c = map_get(&up.data, "close");
            const msgpack_object *r = map_get(&up.data, "reason");
            *close = c && c->type == MSGPACK_OBJECT_BOOLEAN && c->via.boolean;
            if (r && r->type == MSGPACK_OBJECT_STR && whylen) {
                size_t n = r->via.str.size < whylen - 1 ? r->via.str.size : whylen - 1;
                memcpy(why, r->via.str.ptr, n);
                why[n] = '\0';
            }
            found = true;
        }
    }
    msgpack_unpacked_destroy(&up);
    return found;
}

// A move (D15): the image is written and the app stops right here, at the safe
// point, until the kernel tells it to close or to go on. Nothing of the VM
// runs meanwhile (its Tasks included), so what goes on elsewhere and what
// would go on here start from the same state. Messages that arrive meanwhile
// are kept and given back when it goes on. Returns true to close; why says
// why it goes on.
static bool hold_until_told(fmrb_app_task_context_t *ctx, ckpt_app_t *e, char *why, size_t whylen)
{
    fmrb_msg_t *stash = (fmrb_msg_t *)fmrb_sys_malloc(sizeof(fmrb_msg_t) * HOLD_STASH_MAX);
    int kept = 0, dropped = 0;
    bool close = false;
    uint64_t t0 = fmrb_hal_time_get_us();
    uint64_t deadline = t0 + (uint64_t)(e->hold_ms + HOLD_GRACE_MS) * 1000;
    why[0] = '\0';
    for (;;) {
        if (ctx->should_exit) {
            // A kill: the app was asked to end; the image has gone wherever
            // it goes, and this copy ends as it would have anyway.
            close = true;
            break;
        }
        if (fmrb_hal_time_get_us() >= deadline) {
            snprintf(why, whylen, "the kernel did not answer in %d ms", (int)(e->hold_ms + HOLD_GRACE_MS));
            break;
        }
        fmrb_msg_t msg;
        if (fmrb_msg_receive(ctx->app_id, &msg, HOLD_POLL_MS) != FMRB_OK) continue;
        if (finish_message(&msg, &close, why, whylen)) break;
        if (stash && kept < HOLD_STASH_MAX) {
            stash[kept++] = msg;
        } else {
            dropped++;
        }
    }
    FMRB_LOGI(TAG, "[%s] held %u ms after its image; %s", ctx->app_name,
              (unsigned)((fmrb_hal_time_get_us() - t0) / 1000), close ? "closes" : "goes on");
    if (!close) {
        for (int i = 0; i < kept; i++) fmrb_msg_send(ctx->app_id, &stash[i], 0);
    }
    if (dropped) FMRB_LOGW(TAG, "[%s] %d message(s) dropped while held", ctx->app_name, dropped);
    if (stash) fmrb_sys_free(stash);
    if (!close && !why[0]) snprintf(why, whylen, "told to go on");
    return close;
}

// ---- the moving mark (the user's decision on F3) -----------------------------
//
// While a move holds the app (its image is on its way, D15), a small themed
// badge saying so stands over its window's title bar. It is a canvas of its
// own just above the window, so taking it down shows the window exactly as it
// was: the app draws nothing (it is stopped at the safe point and cannot).
// A restart in the same program (F1) is not held and shows none.

#define MARK_TEXT "moving..."
#define MARK_W 58
#define MARK_H 9

static fmrb_canvas_handle_t mark_show(fmrb_app_task_context_t *ctx)
{
    fmrb_gfx_context_t gfx = fmrb_gfx_get_global_context();
    if (!gfx || ctx->headless || ctx->canvas_id == 0) return FMRB_CANVAS_SCREEN;
    fmrb_canvas_handle_t id = FMRB_CANVAS_SCREEN;
    if (fmrb_gfx_create_canvas(gfx, MARK_W, MARK_H, (int16_t)(ctx->z_order + 1), false, 0, &id) != FMRB_GFX_OK) {
        FMRB_LOGW(TAG, "[%s] no canvas for the moving mark", ctx->app_name);
        return FMRB_CANVAS_SCREEN;
    }
    const fmrb_theme_t *t = fmrb_theme_get();
    // In the title bar, left of the close button; at the top left of a
    // fullscreen app (it has no title bar) or of a narrow window.
    int x = ctx->window_pos_x + 1;
    int y = ctx->window_pos_y + 1;
    if (!ctx->fullscreen && ctx->window_width >= MARK_W + 30) {
        x = ctx->window_pos_x + ctx->window_width - MARK_W - 12;
    }
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_rect(&cmd, id, 0, 0, MARK_W, MARK_H, t->highlight, true);
    fmrb_gfx_submit(&cmd);
    fmrb_gfx_cmd_text(&cmd, id, 2, 1, MARK_TEXT, t->text_light, t->highlight, true, FMRB_FONT_SIZE_MEDIUM, 0);
    fmrb_gfx_submit(&cmd);
    fmrb_gfx_cmd_present(&cmd, id, (int16_t)x, (int16_t)y, 0xFF);
    fmrb_gfx_submit(&cmd);
    FMRB_LOGI(TAG, "[%s] moving mark shown (canvas %u)", ctx->app_name, (unsigned)id);
    return id;
}

static void mark_hide(fmrb_app_task_context_t *ctx, fmrb_canvas_handle_t id)
{
    if (id == FMRB_CANVAS_SCREEN) return;
    fmrb_gfx_context_t gfx = fmrb_gfx_get_global_context();
    if (!gfx) return;
    fmrb_gfx_delete_canvas(gfx, id);
    // Deleting a canvas does not make the screen compose again by itself: an
    // app that draws only when something happens left the mark on screen
    // until it presented (as fmrb_kernel_clear_halt says)
    gfx_cmd_t cmd;
    fmrb_gfx_cmd_present(&cmd, 0, 0, 0, 0);
    fmrb_gfx_submit(&cmd);
    FMRB_LOGI(TAG, "[%s] moving mark taken down", ctx->app_name);
}

// The gem's handler: an image of this app was taken at a safe point.
static void ckpt_written(mrb_state *mrb, const uint8_t *buf, size_t len, const char *unused,
                         enum mrb_checkpoint_kind kind, void *ud)
{
    (void)unused; (void)kind; (void)ud;
    fmrb_app_task_context_t *ctx = fmrb_current();
    ckpt_app_t *e = entry_of(ctx);
    if (!ctx || !e) return;

    char stem[48];
    stem_of(e->origin[0] ? e->origin : ctx->filepath, stem, sizeof(stem));
    char path[FMRB_MAX_PATH_LEN], toml[FMRB_MAX_PATH_LEN];
    snprintf(path, sizeof(path), CKPT_DIR "%s.ckpt", stem);
    snprintf(toml, sizeof(toml), CKPT_DIR "%s.toml", stem);

    uint64_t t0 = fmrb_hal_time_get_us();
    fmrb_err_t ret = write_file(path, buf, len);
    if (ret == FMRB_OK) ret = write_attributes(ctx, toml);
    if (ret != FMRB_OK) {
        char why[FMRB_MAX_PATH_LEN + 40];
        snprintf(why, sizeof(why), "cannot write %s (error %d)", path, (int)ret);
        refuse(ctx, why);
        mrb_gv_remove(mrb, mrb_intern_lit(mrb, APP_GV));
        return;   // the program goes on here
    }
    FMRB_LOGI(TAG, "[%s] checkpoint written: %s (%u bytes, %u us to write the file)",
              ctx->app_name, path, (unsigned)len, (unsigned)(fmrb_hal_time_get_us() - t0));

    call_hook(mrb, mrb_gv_get(mrb, mrb_intern_lit(mrb, APP_GV)), "on_checkpoint");
    // The scheduler's clock the image carries (mruby-task's tick; the restored
    // side logs the one it goes on from).
    FMRB_LOGI(TAG, "[%s] image at task tick %u", ctx->app_name, (unsigned)mrb->task.tick);

    bool held = e->hold_ms > 0;
    char why_closed[CKPT_WHAT_MAX];
    why_closed[0] = '\0';
    if (held) {
        snprintf(e->what, sizeof(e->what), "%s", path);
        e->status = FMRB_APP_CKPT_HELD;
        send_kernel(ctx->app_id, "checkpoint_held", "path", path);
        char why[CKPT_WHAT_MAX];
        fmrb_canvas_handle_t mark = mark_show(ctx);
        bool close = hold_until_told(ctx, e, why, sizeof(why));
        if (close) snprintf(why_closed, sizeof(why_closed), "%s", why);
        mark_hide(ctx, mark);
        e->hold_ms = 0;
        if (!close) {
            FMRB_LOGI(TAG, "[%s] goes on from its image: %s", ctx->app_name, why);
            mrb_gv_remove(mrb, mrb_intern_lit(mrb, APP_GV));
            snprintf(e->what, sizeof(e->what), "%s", why);
            e->status = FMRB_APP_CKPT_CONTINUED;
            send_kernel(ctx->app_id, "checkpoint_continued", "reason", why);
            return;   // the program goes on here
        }
    }

    // It ends because it was asked to: the kernel does not report a crash.
    ctx->expected_stop = true;
    end_all_tasks(mrb);

    snprintf(e->what, sizeof(e->what), "%s", path);
    if (held && why_closed[0]) {
        // A move over Asterism (F5): where it went on ("moved to <node> as
        // PID n"), for whoever asked
        snprintf(e->what, sizeof(e->what), "%s", why_closed);
    }
    if (held) {
        e->status = FMRB_APP_CKPT_CLOSED;   // the kernel told it (or a kill did)
    } else {
        e->status = FMRB_APP_CKPT_WRITTEN;
        send_kernel(ctx->app_id, "checkpoint_done", "path", path);
    }
}

// The gem's refusal handler: the request was taken but no image could be
// written at the safe point (a value of a type that cannot be carried...).
static void ckpt_refused(mrb_state *mrb, const char *reason, void *ud)
{
    (void)ud;
    fmrb_app_task_context_t *ctx = fmrb_current();
    mrb_gv_remove(mrb, mrb_intern_lit(mrb, APP_GV));
    if (ctx) refuse(ctx, reason);
}

bool fmrb_app_checkpoint_request(mrb_state *mrb, mrb_value app, int32_t hold_ms)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    ckpt_app_t *e = entry_of(ctx);
    if (!ctx || !e) return false;
    if (!ctx->movable || e->mrb != mrb) {
        refuse(ctx, "the app does not declare movable = true in its .app.toml");
        return false;
    }
    e->hold_ms = hold_ms > 0 ? hold_ms : 0;
    mrb_gv_set(mrb, mrb_intern_lit(mrb, APP_GV), app);
    enum mrb_checkpoint_req r = mrb_checkpoint_request(mrb);
    if (r == MRB_CHECKPOINT_REQ_OK || r == MRB_CHECKPOINT_REQ_PENDING) {
        FMRB_LOGI(TAG, "[%s] checkpoint requested; written at the next safe point", ctx->app_name);
        return true;
    }
    mrb_gv_remove(mrb, mrb_intern_lit(mrb, APP_GV));
    refuse(ctx, r == MRB_CHECKPOINT_REQ_C_BOUNDARY
                    ? "the app waits inside a block called from C (a C frame cannot be carried)"
                    : "the VM is not set up for checkpoints");
    return false;
}

// ---- asking the destination first (F5) -----------------------------------------
//
// A move over Asterism asks the other machine before anything is stopped: the
// method names the program calls (the same list as the image's SND0) and the
// names its .app.toml requires go there, and the other machine answers which
// of them its build lacks. The list is written to /tmp/<name>.calls for the
// transfer app to read: "requires" and the declared names on the first line,
// then one name per line.

typedef struct {
    fmrb_file_t f;
    bool ok;
} calls_out_t;

static void write_call(mrb_state *mrb, const char *name, size_t len, void *ud)
{
    (void)mrb;
    calls_out_t *c = (calls_out_t *)ud;
    size_t done = 0;
    if (fmrb_hal_file_write(c->f, name, len, &done) != FMRB_OK || done != len) c->ok = false;
    if (fmrb_hal_file_write(c->f, "\n", 1, &done) != FMRB_OK || done != 1) c->ok = false;
}

void fmrb_app_checkpoint_calls(mrb_state *mrb)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    ckpt_app_t *e = entry_of(ctx);
    if (!ctx || !e) return;
    if (!ctx->movable || e->mrb != mrb) {
        refuse(ctx, "the app does not declare movable = true in its .app.toml");
        return;
    }
    char stem[48];
    char path[FMRB_MAX_PATH_LEN];
    stem_of(e->origin[0] ? e->origin : ctx->filepath, stem, sizeof(stem));
    snprintf(path, sizeof(path), CKPT_DIR "%s.calls", stem);
    calls_out_t c = { NULL, true };
    if (fmrb_hal_file_open(path, FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_TRUNC, &c.f) != FMRB_OK) {
        char why[FMRB_MAX_PATH_LEN + 16];
        snprintf(why, sizeof(why), "cannot write %s", path);
        refuse(ctx, why);
        return;
    }
    size_t done = 0;
    char head[CKPT_REQUIRES_MAX + 16];
    int hn = snprintf(head, sizeof(head), "requires %s\n", e->requires);
    if (fmrb_hal_file_write(c.f, head, (size_t)hn, &done) != FMRB_OK) c.ok = false;
    int n = mrb_checkpoint_sent_names(mrb, write_call, &c);
    fmrb_hal_file_close(c.f);
    if (n < 0 || !c.ok) {
        fmrb_hal_file_remove(path);
        refuse(ctx, n < 0 ? "the VM has no baseline to name the methods it calls" : "cannot write the method names");
        return;
    }
    FMRB_LOGI(TAG, "[%s] the %d method names the program calls are in %s%s%s", ctx->app_name, n, path,
              e->requires[0] ? "; it requires " : "", e->requires);
    send_kernel(ctx->app_id, "checkpoint_calls", "path", path);
}

void fmrb_app_checkpoint_untaken(mrb_state *mrb, const char *reason)
{
    fmrb_app_task_context_t *ctx = fmrb_current();
    ckpt_app_t *e = entry_of(ctx);
    if (!ctx || !e || e->mrb != mrb) return;
    FMRB_LOGW(TAG, "[%s] not moved (it goes on): %s", ctx->app_name, reason);
    snprintf(e->what, sizeof(e->what), "%s", reason);
    e->status = FMRB_APP_CKPT_REFUSED;
}

// ---- the receiving side's methods (F5) ------------------------------------------
//
// The transfer app (/usr/share/transfer/transfer.app.rb) answers the other
// machines from these. They are defined only in a VM set up for checkpoints
// (a movable app, an image being restored), before its baseline is taken, so
// that the other apps' VMs do not carry them.

// FmrbApp._checkpoint_has_method(name) -> true | false | nil
// Whether a class or module of this VM's baseline defines a method of that
// name (nil: no baseline): the answer to the sender's list before anything is
// stopped.
static mrb_value app_s_checkpoint_has_method(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    const char *name;
    mrb_int len;
    mrb_get_args(mrb, "s", &name, &len);
    int r = mrb_checkpoint_has_method(mrb, name, (size_t)len);
    return r < 0 ? mrb_nil_value() : mrb_bool_value(r > 0);
}

// FmrbApp._checkpoint_restore(path) -> nil | String
// Ask the kernel to start the image at path (a .ckpt under /tmp/), as the
// debug server's restore does. nil when asked; _checkpoint_restore_poll has
// the answer.
static mrb_value app_s_checkpoint_restore(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    const char *path;
    mrb_get_args(mrb, "z", &path);
    if (fmrb_app_checkpoint_restore_ask(path) != FMRB_OK) {
        return mrb_str_new_lit(mrb, "the image cannot be asked to start (not a .ckpt, or no kernel)");
    }
    return mrb_nil_value();
}

// FmrbApp._checkpoint_restore_poll -> [status, pid, what]
// status: "none", "pending", "restored" (what: the names this build lacks,
// one space between them) or "refused" (what: why).
static mrb_value app_s_checkpoint_restore_poll(mrb_state *mrb, mrb_value klass)
{
    (void)klass;
    int32_t pid = -1;
    char what[CKPT_WHAT_MAX];
    fmrb_app_restore_status_t st = fmrb_app_checkpoint_restore_poll(&pid, what, sizeof(what));
    const char *name = st == FMRB_APP_RESTORE_PENDING ? "pending"
                     : st == FMRB_APP_RESTORE_DONE ? "restored"
                     : st == FMRB_APP_RESTORE_REFUSED ? "refused" : "none";
    mrb_value out = mrb_ary_new_capa(mrb, 3);
    mrb_ary_push(mrb, out, mrb_str_new_cstr(mrb, name));
    mrb_ary_push(mrb, out, mrb_fixnum_value(pid));
    mrb_ary_push(mrb, out, mrb_str_new_cstr(mrb, what));
    return out;
}

static void define_receiving_methods(mrb_state *mrb)
{
    if (!mrb_class_defined(mrb, "FmrbApp")) return;
    struct RClass *app = mrb_class_get(mrb, "FmrbApp");
    mrb_define_class_method(mrb, app, "_checkpoint_has_method", app_s_checkpoint_has_method, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app, "_checkpoint_restore", app_s_checkpoint_restore, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, app, "_checkpoint_restore_poll", app_s_checkpoint_restore_poll, MRB_ARGS_NONE());
}

// ---- setting a VM up ------------------------------------------------------------

// What the app's pool has in use (the estalloc of fmrb_app.c), to log the
// baseline's cost.
extern int mrb_get_estalloc_stats(void* est_ptr, size_t* total, size_t* used, size_t* free, int32_t* frag);

static size_t pool_used(const fmrb_app_task_context_t *ctx)
{
    size_t total = 0, used = 0, free_bytes = 0;
    int32_t frag = 0;
    if (ctx->est) mrb_get_estalloc_stats(ctx->est, &total, &used, &free_bytes, &frag);
    return used;
}

// The pool's high-water mark so far (estalloc), to log what the baseline asks
// for while it is made.
extern int mrb_get_estalloc_usage(void* est_ptr, size_t* peak, size_t* largest_free, size_t* last_fail,
                                  size_t* fails);

static size_t pool_peak(const fmrb_app_task_context_t *ctx)
{
    size_t peak = 0, largest = 0, last_fail = 0, fails = 0;
    if (ctx->est) mrb_get_estalloc_usage(ctx->est, &peak, &largest, &last_fail, &fails);
    return peak;
}

// The permanent names, counted and hashed (FNV-1a over each name and its kind):
// two programs of the same build must find the same ones (F2), and the log
// shows it.
typedef struct {
    size_t n;
    uint32_t digest;
} name_sum_t;

static void count_name(const char *name, int is_func, void *ud)
{
    name_sum_t *sum = (name_sum_t *)ud;
    uint32_t h = sum->digest;
    for (const char *p = name; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    h = (h ^ (is_func ? 1u : 0u)) * 16777619u;
    sum->digest = h;
    sum->n++;
}

// The permanent names in full, for comparing two builds (doc/checkpoint F4):
// when /tmp/ckpt_names exists, a VM set up for checkpoints writes its names to
// /tmp/<app>.names, one per line ("F " a C function, "O " anything else).
// Nothing is written otherwise; the check is one failed open per movable app.
#define NAMES_MARK CKPT_DIR "ckpt_names"

static void write_name(const char *name, int is_func, void *ud)
{
    fmrb_file_t f = (fmrb_file_t)ud;
    size_t done = 0;
    fmrb_hal_file_write(f, is_func ? "F " : "O ", 2, &done);
    fmrb_hal_file_write(f, name, strlen(name), &done);
    fmrb_hal_file_write(f, "\n", 1, &done);
}

static void dump_names(fmrb_app_task_context_t *ctx)
{
    fmrb_file_t mark = NULL;
    if (fmrb_hal_file_open(NAMES_MARK, FMRB_O_RDONLY, &mark) != FMRB_OK) return;
    fmrb_hal_file_close(mark);
    char stem[48];
    char path[FMRB_MAX_PATH_LEN];
    stem_of(ctx->filepath, stem, sizeof(stem));
    snprintf(path, sizeof(path), CKPT_DIR "%s.names", stem);
    fmrb_file_t f = NULL;
    if (fmrb_hal_file_open(path, FMRB_O_WRONLY | FMRB_O_CREAT | FMRB_O_TRUNC, &f) != FMRB_OK) return;
    mrb_checkpoint_each_name(ctx->mrb, write_name, f);
    fmrb_hal_file_close(f);
    FMRB_LOGI(TAG, "[%s] permanent names written to %s", ctx->app_name, path);
}

// The fixed table of this build's baseline (doc/checkpoint/flash_table.md, Ff):
// C source the build makes from a host build of the same gems
// (tools/ckpt_table/), linked in as constant data. Weak: a build without it
// (the first pass of the Linux simulation's build, a device build before F6)
// takes the baseline as before.
extern const mrb_checkpoint_table fmrb_checkpoint_table __attribute__((weak));

// What a VM set up for checkpoints has before its baseline, beyond what
// mrb_open made: the gem's Checkpoint module, the C data types it carries and
// the receiving side's methods. The table maker (tools/ckpt_table/) calls
// this too, so that its baseline is the one the firmware's VMs take.
void fmrb_app_checkpoint_prepare_vm(mrb_state *mrb)
{
    mrb_checkpoint_open(mrb);
    fmrb_gfx_checkpoint_register(mrb);
    define_receiving_methods(mrb);
}

// What the baseline keeps in the pool, by part, on one line (for measuring,
// doc/checkpoint/report/ff.md).
typedef struct {
    char text[320];
    size_t len;
} size_line_t;

static void add_size_part(const char *part, size_t count, size_t bytes, void *ud)
{
    size_line_t *l = (size_line_t *)ud;
    (void)count;
    if (l->len >= sizeof(l->text)) return;
    int n = snprintf(l->text + l->len, sizeof(l->text) - l->len, "%s%s %u", l->len ? ", " : "", part,
                     (unsigned)bytes);
    if (n > 0) l->len += (size_t)n;
}

void fmrb_app_checkpoint_vm_opened(fmrb_app_task_context_t *ctx)
{
    ckpt_app_t *e = entry_of(ctx);
    if (!e || !ctx->mrb) return;
    bool restoring = ctx->load_mode == FMRB_LOAD_MODE_CHECKPOINT;
    if (!ctx->movable && !restoring) return;   // D12: only declared apps

    mrb_state *mrb = ctx->mrb;
    // The last answer (status, what) stays for the debug server: a restart
    // can reuse the slot before it has read it.
    e->mrb = mrb;
    e->millis_offset = 0;
    e->hold_ms = 0;
    e->origin[0] = '\0';
    if (!restoring) snprintf(e->origin, sizeof(e->origin), "%s", ctx->filepath);
    read_requires(ctx->filepath, e->requires, sizeof(e->requires));

    size_t before = pool_used(ctx);
    fmrb_app_checkpoint_prepare_vm(mrb);
    mrb_checkpoint_set_allocator(mrb, work_realloc, NULL);
    mrb_checkpoint_set_embedder(mrb, emb_dump, emb_restore, NULL);
    mrb_checkpoint_set_handler(mrb, ckpt_written, NULL);
    mrb_checkpoint_set_refusal_handler(mrb, ckpt_refused, NULL);
    if (&fmrb_checkpoint_table != NULL) mrb_checkpoint_set_table(mrb, &fmrb_checkpoint_table);
    size_t peak_before = pool_peak(ctx);
    mrb_checkpoint_baseline(mrb);
    name_sum_t names = { 0, 2166136261u };
    mrb_checkpoint_each_name(mrb, count_name, &names);
    const char *why = mrb_checkpoint_table_status(mrb);
    FMRB_LOGI(TAG, "[%s] VM set up for checkpoints%s: the baseline (%u names, digest %08x) took %u bytes of the pool "
              "(%u more while it was made); table: %s",
              ctx->app_name, restoring ? " (restoring an image)" : "", (unsigned)names.n,
              (unsigned)names.digest, (unsigned)(pool_used(ctx) - before),
              (unsigned)(pool_peak(ctx) > peak_before ? pool_peak(ctx) - pool_used(ctx) : 0),
              why ? why : "used");
    size_line_t parts = { "", 0 };
    mrb_checkpoint_baseline_size(mrb, add_size_part, &parts);
    FMRB_LOGI(TAG, "[%s] baseline by part (bytes): %s", ctx->app_name, parts.text);
    dump_names(ctx);
}

void fmrb_app_checkpoint_vm_closing(fmrb_app_task_context_t *ctx)
{
    ckpt_app_t *e = entry_of(ctx);
    if (e && e->mrb == ctx->mrb) e->mrb = NULL;
}

void fmrb_app_checkpoint_add_source(fmrb_app_task_context_t *ctx, const char *src, size_t len)
{
    ckpt_app_t *e = entry_of(ctx);
    if (!e || e->mrb != ctx->mrb || !ctx->mrb) return;
    mrb_checkpoint_add_source(ctx->mrb, ctx->filepath, src, len);
}

void fmrb_app_checkpoint_host_task(fmrb_app_task_context_t *ctx, mrb_value task)
{
    ckpt_app_t *e = entry_of(ctx);
    if (!e || e->mrb != ctx->mrb || !ctx->mrb) return;
    mrb_checkpoint_set_host_task(ctx->mrb, task);
}

// ---- restoring ------------------------------------------------------------------

static uint8_t *read_image(const char *path, size_t *len)
{
    fmrb_file_t f = NULL;
    uint32_t size = 0;
    if (fmrb_hal_file_open(path, FMRB_O_RDONLY, &f) != FMRB_OK) return NULL;
    if (fmrb_hal_file_size(f, &size) != FMRB_OK || size == 0) {
        fmrb_hal_file_close(f);
        return NULL;
    }
    uint8_t *buf = (uint8_t *)fmrb_sys_malloc(size);
    size_t got = 0;
    if (buf && (fmrb_hal_file_read(f, buf, size, &got) != FMRB_OK || got != size)) {
        fmrb_sys_free(buf);
        buf = NULL;
    }
    fmrb_hal_file_close(f);
    *len = size;
    return buf;
}

typedef struct {
    const uint8_t *buf;
    size_t len;
} image_t;

// The names mrb_checkpoint_missing_names gives, one space between them, as
// many as fit.
typedef struct {
    char *out;
    size_t room;
    size_t used;
    int cut;
} missing_t;

static void add_missing(mrb_state *mrb, const char *name, size_t len, void *ud)
{
    (void)mrb;
    missing_t *m = (missing_t *)ud;
    if (m->used + len + 2 > m->room) { m->cut = 1; return; }
    if (m->used) m->out[m->used++] = ' ';
    memcpy(m->out + m->used, name, len);
    m->used += len;
    m->out[m->used] = '\0';
}

static mrb_value restore_body(mrb_state *mrb, void *ud)
{
    image_t *im = (image_t *)ud;
    mrb_checkpoint_restore(mrb, im->buf, im->len);
    return mrb_nil_value();
}

static mrb_value continue_body(mrb_state *mrb, void *ud)
{
    (void)ud;
    return mrb_checkpoint_continue(mrb);
}

int fmrb_app_checkpoint_resume(fmrb_app_task_context_t *ctx, mrb_value *task, char *err, size_t errlen,
                               bool *answered)
{
    mrb_state *mrb = ctx->mrb;
    ckpt_app_t *e = entry_of(ctx);
    *task = mrb_nil_value();
    *answered = false;
    if (!e || e->mrb != mrb) {
        snprintf(err, errlen, "the VM is not set up for checkpoints");
        *answered = restore_answer(ctx->filepath, FMRB_APP_RESTORE_REFUSED, ctx->app_id, err);
        return -1;
    }

    // What the app declares it cannot go on without (requires in its .app.toml,
    // carried in the image's .toml): refused here, before anything is restored,
    // when this build has no method of that name. Other names the program calls
    // and this build lacks are only told (below).
    const char *p = e->requires;
    while (*p) {
        size_t n = strcspn(p, " ");
        if (mrb_checkpoint_has_method(mrb, p, n) == 0) {
            snprintf(err, errlen, "the app requires the method '%.*s' (requires in its .app.toml), "
                     "which this build does not have", (int)n, p);
            *answered = restore_answer(ctx->filepath, FMRB_APP_RESTORE_REFUSED, ctx->app_id, err);
            return -1;
        }
        p += n;
        while (*p == ' ') p++;
    }

    size_t len = 0;
    uint8_t *buf = read_image(ctx->filepath, &len);
    if (!buf) {
        snprintf(err, errlen, "cannot read the image %s", ctx->filepath);
        *answered = restore_answer(ctx->filepath, FMRB_APP_RESTORE_REFUSED, ctx->app_id, err);
        return -1;
    }
    uint64_t t0 = fmrb_hal_time_get_us();
    image_t im = { buf, len };
    mrb_bool failed = FALSE;
    mrb_value r = mrb_protect_error(mrb, restore_body, &im, &failed);
    fmrb_sys_free(buf);   // nothing refers to it after the restore (sources are copied)
    if (failed) {
        mrb_value msg = mrb_funcall(mrb, r, "message", 0);
        snprintf(err, errlen, "cannot restore %s: %s", ctx->filepath,
                 mrb_string_p(msg) ? RSTRING_PTR(msg) : "?");
        *answered = restore_answer(ctx->filepath, FMRB_APP_RESTORE_REFUSED, ctx->app_id, err);
        return -1;
    }
    FMRB_LOGI(TAG, "[%s] restored %s (%u bytes) in %u us, task tick %u", ctx->app_name, ctx->filepath,
              (unsigned)len, (unsigned)(fmrb_hal_time_get_us() - t0), (unsigned)mrb->task.tick);
    // Methods the program calls that this build has none of (the image's
    // SND0): not refused, as a movable app guards the calls that are not
    // common to every build (respond_to?, rescue NoMethodError). Logged here,
    // and told to whoever asked for the image.
    char missing[CKPT_WHAT_MAX];
    missing_t m = { missing, sizeof(missing), 0, 0 };
    missing[0] = '\0';
    int nmissing = mrb_checkpoint_missing_names(mrb, add_missing, &m);
    if (nmissing > 0) {
        FMRB_LOGW(TAG, "[%s] the program calls %d method(s) this build does not have: %s%s "
                  "(it goes on; a call that is not guarded raises NoMethodError)",
                  ctx->app_name, nmissing, missing, m.cut ? " ..." : "");
    }
    // The image is up here: whoever asked for it (a move) hears it now, before
    // the program goes on.
    restore_answer(ctx->filepath, FMRB_APP_RESTORE_DONE, ctx->app_id, missing);

    *task = mrb_checkpoint_host_task(mrb);
    mrb_value app = mrb_gv_get(mrb, mrb_intern_lit(mrb, APP_GV));
    mrb_gv_remove(mrb, mrb_intern_lit(mrb, APP_GV));

    // FmrbApp makes again what it kept outside the VM (its window, F3) and
    // calls the app's on_restore (_checkpoint_restored); anything else that is
    // not an FmrbApp gets on_restore alone.
    if (mrb->c == mrb->root_c) {
        bool base = !mrb_nil_p(app) &&
                    mrb_respond_to(mrb, app, mrb_intern_lit(mrb, "_checkpoint_restored"));
        call_hook(mrb, app, base ? "_checkpoint_restored" : "on_restore");
    }
    // The app is up (its window's canvas has told the kernel already; a
    // headless one is told here, as FmrbApp#_init's canvas would).
    fmrb_app_notify_started(ctx);

    mrb_bool cfailed = FALSE;
    mrb_value cr = mrb_protect_error(mrb, continue_body, NULL, &cfailed);
    if (cfailed) mrb->exc = mrb_obj_ptr(cr);   // reported like any app's exception
    return 0;
}

// ---- asking for an image (the debug server, the apps) ------------------------

// Every way of asking for an image comes here: the debug server
// (fmrb_app_checkpoint_ask) and the apps (FmrbApp.checkpoint: the
// shell's `checkpoint`, an app asking for its own image). The kernel is told
// {"cmd" => "checkpoint", "pid", "restart"} and, when an app asked,
// "from" => its pid: the kernel answers it with "checkpoint_result". Returns
// FMRB_OK with *why NULL when the kernel was asked; FMRB_OK with *why when the
// app cannot be written (D16: the answer is in the table as well, as the
// app's own refusals are); an error with *why when nothing was asked.
static fmrb_err_t ask_kernel(int32_t pid, bool restart, int32_t hold_ms, int32_t reply_to, const char **why)
{
    *why = NULL;
    fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id(pid);
    ckpt_app_t *e = entry_of(ctx);
    if (!ctx || !e || ctx->state != PROC_STATE_RUNNING) {
        *why = "no running app with that PID";
        return FMRB_ERR_INVALID_PARAM;
    }
    if (ctx->vm_type != FMRB_VM_TYPE_MRUBY) {
        // D16: a Spinel program (and the other engines) cannot be carried
        snprintf(e->what, sizeof(e->what), "only an app on the mruby VM can be written");
        e->status = FMRB_APP_CKPT_REFUSED;
        *why = e->what;
        return FMRB_OK;
    }
    e->what[0] = '\0';
    e->status = FMRB_APP_CKPT_PENDING;

    ctl_msg_t m;
    ctl_begin(&m, PROC_ID_KERNEL, 3 + (reply_to >= 0 ? 1 : 0) + (hold_ms > 0 ? 1 : 0));
    ctl_str(&m, "cmd");
    ctl_str(&m, "checkpoint");
    ctl_str(&m, "pid");
    ctl_int(&m, (uint32_t)pid);
    ctl_str(&m, "restart");
    ctl_bool(&m, restart);
    if (reply_to >= 0) {
        ctl_str(&m, "from");
        ctl_int(&m, (uint32_t)reply_to);
    }
    if (hold_ms > 0) {
        ctl_str(&m, "hold");
        ctl_int(&m, (uint32_t)hold_ms);
    }
    if (ctl_send(&m, PROC_ID_KERNEL) != FMRB_OK) {
        e->status = FMRB_APP_CKPT_NONE;
        *why = "could not reach the kernel";
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

fmrb_err_t fmrb_app_checkpoint_ask(int32_t pid, bool restart, int32_t hold_ms)
{
    const char *why;
    return ask_kernel(pid, restart, hold_ms, -1, &why);   // a refusal is the answer the table holds
}

const char *fmrb_app_checkpoint_ask_from_app(int32_t pid, bool restart, int32_t reply_to)
{
    const char *why;
    ask_kernel(pid, restart, 0, reply_to, &why);
    return why;
}

// A move over Asterism (F5): the kernel asks the destination first, then has
// the app write its image and hold, has the transfer app carry it, and closes
// the app once it is up there (fmrb_kernel.rb, start_move). Checked here as
// far as C can: a running app on mruby that declares movable.
static fmrb_err_t ask_move(int32_t pid, const char *to, int32_t hold_ms, int32_t reply_to, const char **why)
{
    *why = NULL;
    fmrb_app_task_context_t *ctx = fmrb_app_get_context_by_id(pid);
    ckpt_app_t *e = entry_of(ctx);
    if (!ctx || !e || ctx->state != PROC_STATE_RUNNING) {
        *why = "no running app with that PID";
        return FMRB_ERR_INVALID_PARAM;
    }
    size_t tl = to ? strlen(to) : 0;
    if (tl == 0 || tl > FMRB_APP_MOVE_TO_MAX) {
        *why = "no destination (the Asterism node ID of the other machine)";
        return FMRB_ERR_INVALID_PARAM;
    }
    if (ctx->vm_type != FMRB_VM_TYPE_MRUBY || !ctx->movable) {
        snprintf(e->what, sizeof(e->what), "%s", ctx->vm_type != FMRB_VM_TYPE_MRUBY
                 ? "only an app on the mruby VM can be written"
                 : "the app does not declare movable = true in its .app.toml");
        e->status = FMRB_APP_CKPT_REFUSED;
        *why = e->what;
        return FMRB_OK;
    }
    e->what[0] = '\0';
    e->status = FMRB_APP_CKPT_PENDING;
    ctl_msg_t m;
    ctl_begin(&m, PROC_ID_KERNEL, 4 + (reply_to >= 0 ? 1 : 0));
    ctl_str(&m, "cmd");
    ctl_str(&m, "move");
    ctl_str(&m, "pid");
    ctl_int(&m, (uint32_t)pid);
    ctl_str(&m, "to");
    ctl_str(&m, to);
    ctl_str(&m, "hold");
    ctl_int(&m, (uint32_t)(hold_ms > 0 ? hold_ms : 0));
    if (reply_to >= 0) {
        ctl_str(&m, "from");
        ctl_int(&m, (uint32_t)reply_to);
    }
    if (ctl_send(&m, PROC_ID_KERNEL) != FMRB_OK) {
        e->status = FMRB_APP_CKPT_NONE;
        *why = "could not reach the kernel";
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

fmrb_err_t fmrb_app_checkpoint_move(int32_t pid, const char *to, int32_t hold_ms)
{
    const char *why;
    return ask_move(pid, to, hold_ms, -1, &why);   // a refusal is the answer the table holds
}

const char *fmrb_app_checkpoint_move_from_app(int32_t pid, const char *to, int32_t hold_ms, int32_t reply_to)
{
    const char *why;
    ask_move(pid, to, hold_ms, reply_to, &why);
    return why;
}

fmrb_err_t fmrb_app_checkpoint_finish(int32_t pid, bool close, const char *reason)
{
    if (pid < 0 || pid >= FMRB_MAX_APPS) return FMRB_ERR_INVALID_PARAM;
    ctl_msg_t m;
    ctl_begin(&m, PROC_ID_KERNEL, 4);
    ctl_str(&m, "cmd");
    ctl_str(&m, "checkpoint_finish");
    ctl_str(&m, "pid");
    ctl_int(&m, (uint32_t)pid);
    ctl_str(&m, "close");
    ctl_bool(&m, close);
    ctl_str(&m, "reason");
    ctl_str(&m, reason ? reason : "");
    return ctl_send(&m, PROC_ID_KERNEL) == FMRB_OK ? FMRB_OK : FMRB_ERR_FAILED;
}

// ---- starting an image (the receiving side of a move) -------------------------

// Returns true when someone asked for this image (fmrb_app_checkpoint_restore_ask)
// and now has the answer.
static bool restore_answer(const char *path, int status, int32_t pid, const char *what)
{
    if (s_restore.status != FMRB_APP_RESTORE_PENDING || strcmp(s_restore.path, path) != 0) return false;
    s_restore.pid = pid;
    snprintf(s_restore.what, sizeof(s_restore.what), "%s", what ? what : "");
    s_restore.status = status;
    return true;
}

fmrb_err_t fmrb_app_checkpoint_restore_ask(const char *path)
{
    size_t n = path ? strlen(path) : 0;
    if (n < 6 || n >= sizeof(s_restore.path) || strcmp(path + n - 5, ".ckpt") != 0) {
        return FMRB_ERR_INVALID_PARAM;
    }
    snprintf(s_restore.path, sizeof(s_restore.path), "%s", path);
    s_restore.what[0] = '\0';
    s_restore.pid = -1;
    s_restore.status = FMRB_APP_RESTORE_PENDING;
    ctl_msg_t m;
    ctl_begin(&m, PROC_ID_KERNEL, 2);
    ctl_str(&m, "cmd");
    ctl_str(&m, "restore");
    ctl_str(&m, "path");
    ctl_str(&m, path);
    if (ctl_send(&m, PROC_ID_KERNEL) != FMRB_OK) {
        s_restore.status = FMRB_APP_RESTORE_NONE;
        return FMRB_ERR_FAILED;
    }
    return FMRB_OK;
}

fmrb_app_restore_status_t fmrb_app_checkpoint_restore_poll(int32_t *pid, char *what, size_t what_len)
{
    int st = s_restore.status;
    if (pid) *pid = s_restore.pid;
    if (what && what_len) snprintf(what, what_len, "%s", s_restore.what);
    return (fmrb_app_restore_status_t)st;
}

void fmrb_app_checkpoint_spawn_failed(const char *path, fmrb_err_t err)
{
    char why[64];
    snprintf(why, sizeof(why), "could not start the image (error %d)", (int)err);
    restore_answer(path, FMRB_APP_RESTORE_REFUSED, -1, why);
}

fmrb_app_ckpt_status_t fmrb_app_checkpoint_poll(int32_t pid, char *what, size_t what_len)
{
    if (pid < 0 || pid >= FMRB_MAX_APPS) return FMRB_APP_CKPT_NONE;
    ckpt_app_t *e = &s_ckpt[pid];
    int st = e->status;
    if (what && what_len) snprintf(what, what_len, "%s", e->what);
    return (fmrb_app_ckpt_status_t)st;
}

#endif /* FMRB_HAVE_CHECKPOINT */
