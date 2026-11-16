/*
 * wasm apps on WAMR's classic interpreter (doc/wasm_apps W1b).
 *
 * The runtime: one per process (WAMR's state is static). The first wasm app
 * starts it, the last one ends it (decision 4). Its pool -- module data,
 * instances, exec envs and the operand stacks -- is FMRB_WASM_POOL_SIZE of
 * PSRAM, taken by the first wasm app and kept after that (it is reused by
 * the next start, so it is taken once per boot). The boundary's natives
 * (the generated fmrb_boundary_wasm.inc) are registered raw under the import
 * module "fmrb" at each start, from a copy in that pool (WAMR sorts the
 * table in place, and a writable table in the image would be .data in
 * internal RAM).
 *
 * An app: its .wasm is read into the app's own memory pool (the classic
 * interpreter rewrites the copy as it runs), and its linear memory is mapped
 * from the same pool by the platform layer (platform/fmrb_wamr_platform.c).
 * The upper bound of the linear memory is what the pool leaves after the
 * copy (decision 5): the instance gets a maximum page count from it.
 *
 * The app runs on the app shell (fmrb_app_shell.h), as a Lua app does. The
 * hooks are the ones the generated SDK header (sdk/c/fmrb_boundary.h) ends
 * with, from sig/ (doc/app_boundary W4b-3):
 *
 *   _initialize()                  run once first, when exported (TinyGo,
 *                                  WASI reactors): the shell's LOAD
 *   on_create()
 *   on_update() -> i32             ms until the next call (negative: the
 *                                  default FMRB_SHELL_DEFAULT_UPDATE_MS)
 *   on_event(const fmrb_app_event_t *ev)
 *   on_destroy(), on_suspend(), on_resume(), on_quit_request()
 *   on_resize(new_width, new_height)
 *
 * on_event's record (60 bytes, fmrb_bnd_app_event_t here) goes into the
 * static buffer the SDK exports (decision 20): the module's
 * fmrb_event_buffer() -> i32 (FMRB_BND_EVENT_BUFFER_EXPORT) gives its
 * address, asked once before the first event. A module without it gets the
 * record on its own C stack: below the value of its stack pointer global
 * (the first mutable i32 global the module defines: LLVM's __stack_pointer),
 * which is lowered over the call and put back after it, as a C caller
 * passing a pointer to a local does. A module with on_event and neither
 * (a small app whose optimiser dropped the stack pointer, built with an
 * older SDK) is refused at load. The type is
 * the record's choice number (FMRB_BND_APP_EVENT_TYPE_*, the order Lua's
 * strings have); the fields each kind fills are Lua's (the others are 0):
 *   key_down / key_up:      keycode, scancode, modifier, character
 *   mouse_down / mouse_up:  button, x, y, wx, wy (x, y in the app's area;
 *                           wx, wy in the window)
 *   mouse_move:             x, y, wx, wy
 *   mouse_wheel:            delta, x, y, wx, wy
 *   gamepad_down / _up:     gamepad_id, button
 *   gamepad_axis:           gamepad_id, axis, value
 *   kana_mode:              mode
 *
 * The boundary's FmrbApp functions that work on the window (the area, stop,
 * the frame, clear_user_area_raw) need the shell (ctx->shell): every hook is
 * called from inside fmrb_app_shell_run, so they work from any hook.
 *
 * The window's FmrbGfx is drawer 0 (the shell's layout opens it before
 * on_create; no other drawer is open then): every fmrb_gfx_* call of the app
 * takes 0 as its receiver.
 *
 * A call that traps, or runs past FMRB_WASM_CALL_INSN_LIMIT instructions,
 * ends the app with the reason (decision 3): it is not called again.
 */
/* pthread_getattr_np on the hosted native build (ESP-IDF's linux target
   defines it already). */
#if defined(CONFIG_IDF_TARGET_LINUX) && !defined(__EMSCRIPTEN__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "fmrb_wasm.h"

#include <stdio.h>
#include <string.h>

#include "wasm_export.h"
/* The interpreter's instance: its globals (the app's stack pointer). */
#include "wasm_runtime.h"

#include "fmrb_app.h"
#include "fmrb_app_shell.h"
#include "fmrb_gfx_drawer.h"
#include "fmrb_hal_file.h"
#include "fmrb_hal_time.h"
#include "fmrb_hid_decode.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_rtos.h"

#if !defined(CONFIG_IDF_TARGET_LINUX)
#include "esp_heap_caps.h"
#elif defined(__EMSCRIPTEN__)
#include <emscripten/stack.h>
#else
#include <pthread.h>
#endif

/* The generated raw natives of the boundary (tools/boundary/gen.rb). */
#include "fmrb_gen/fmrb_boundary.h"
#include "fmrb_gen/fmrb_boundary_wasm.inc"

static const char *TAG = "wasm";

_Static_assert(sizeof(fmrb_bnd_app_event_t) <= FMRB_BND_EVENT_BUFFER_SIZE,
               "the SDK's event buffer holds the event record");

/** The runtime's shared pool: about 28 KB per running app (16 KB of it the
 *  operand stack) plus the module data (report/w1a.md 8). */
#define FMRB_WASM_POOL_SIZE (256 * 1024)
/** The operand stack of an instance, from the shared pool. */
#define FMRB_WASM_OPERAND_STACK (16 * 1024)
/** Instructions one call of the app may run before it is stopped. */
#define FMRB_WASM_CALL_INSN_LIMIT (20 * 1000 * 1000)
/** Native stack left below the boundary for the interpreter's own frames
 *  and the boundary functions it calls (WAMR adds no guard to a boundary
 *  that is set by hand). */
#define FMRB_WASM_NATIVE_STACK_GUARD (6 * 1024)
/** Room left in the app's pool for the rest (logs, the shell, the
 *  boundary's own allocations) when the linear memory's bound is worked out. */
#define FMRB_WASM_POOL_MARGIN (64 * 1024)
#define WASM_PAGE_SIZE (64 * 1024)

/* The boundary's natives as the image holds them (rodata, flash). */
static const NativeSymbol k_natives[] = { FMRB_GEN_WASM_NATIVES };
#define NATIVES_COUNT (sizeof(k_natives) / sizeof(k_natives[0]))

/* The runtime's state (PSRAM .bss on the ESP32: fmrb_wamr_psram.lf). */
static fmrb_semaphore_t s_lock;
static StaticSemaphore_t s_lock_storage;
static int s_users;
static void *s_pool;
static NativeSymbol *s_natives;
/* The apps that count as users, so a forced kill can give its count back
   (fmrb_wasm_app_forget). */
static fmrb_app_task_context_t *s_holders[FMRB_MAX_APPS];

/* What the System Monitor, the shell and the periodic dump show of each
   holder (fmrb_wasm_app_stats), at the holder's index. inst is set while the
   app's module is instantiated, under s_lock; the timings are written by the
   app's own task only and read as they are. */
typedef struct {
    wasm_module_inst_t inst;
    uint32_t binary_size;
    uint32_t max_pages;
    uint32_t hook_us;
    uint32_t hook_max_us;
    uint32_t calls;
} wasm_slot_t;
static wasm_slot_t s_slots[FMRB_MAX_APPS];
/* Since boot: calls that trapped, and the shared pool's high mark of the
   runtimes that have ended (each start lays the pool out afresh). */
static uint32_t s_traps;
static uint32_t s_pool_high;

/** How long a start waits for the runtime's lock before it gives up (only a
 *  task killed inside the start or the end of another wasm app holds it
 *  for longer than a moment). */
#define FMRB_WASM_LOCK_WAIT_MS 2000

void fmrb_wasm_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    }
    FMRB_LOGI(TAG, "WAMR classic interpreter linked (pool %u KB, taken by the first wasm app)",
              (unsigned)(FMRB_WASM_POOL_SIZE / 1024));
}

static void *pool_alloc(size_t size)
{
#if !defined(CONFIG_IDF_TARGET_LINUX)
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

static bool runtime_acquire(fmrb_app_task_context_t *ctx, char *err, size_t err_cap)
{
    if (!s_lock) {
        snprintf(err, err_cap, "wasm: the runtime was not set up at boot");
        return false;
    }
    if (fmrb_semaphore_take(s_lock, FMRB_MS_TO_TICKS(FMRB_WASM_LOCK_WAIT_MS)) != pdTRUE) {
        snprintf(err, err_cap, "wasm: the runtime's lock is held (an app killed in its start?)");
        return false;
    }
    bool ok = true;
    if (s_users == 0) {
        if (!s_pool) {
            s_pool = pool_alloc(FMRB_WASM_POOL_SIZE);
        }
        RuntimeInitArgs init;
        memset(&init, 0, sizeof(init));
        init.mem_alloc_type = Alloc_With_Pool;
        init.mem_alloc_option.pool.heap_buf = s_pool;
        init.mem_alloc_option.pool.heap_size = FMRB_WASM_POOL_SIZE;
        if (!s_pool) {
            snprintf(err, err_cap, "wasm: no %u KB of PSRAM for the runtime's pool",
                     (unsigned)(FMRB_WASM_POOL_SIZE / 1024));
            ok = false;
        } else if (!wasm_runtime_full_init(&init)) {
            snprintf(err, err_cap, "wasm: the runtime did not start");
            ok = false;
        } else {
            s_natives = wasm_runtime_malloc(sizeof(k_natives));
            if (s_natives) {
                memcpy(s_natives, k_natives, sizeof(k_natives));
            }
            if (!s_natives ||
                !wasm_runtime_register_natives_raw("fmrb", s_natives, NATIVES_COUNT)) {
                snprintf(err, err_cap, "wasm: the boundary's functions were not registered");
                if (s_natives) wasm_runtime_free(s_natives);
                s_natives = NULL;
                wasm_runtime_destroy();
                ok = false;
            } else {
                mem_alloc_info_t info;
                unsigned used = wasm_runtime_get_mem_alloc_info(&info)
                                    ? (unsigned)(info.total_size - info.total_free_size) : 0u;
                FMRB_LOGI(TAG, "runtime started (pool %u KB at %p, %u boundary functions, "
                          "table %u bytes, pool used %u bytes)",
                          (unsigned)(FMRB_WASM_POOL_SIZE / 1024), s_pool,
                          (unsigned)NATIVES_COUNT, (unsigned)sizeof(k_natives), used);
            }
        }
    }
    if (ok) {
        s_users++;
        for (int i = 0; i < FMRB_MAX_APPS; i++) {
            if (!s_holders[i]) {
                s_holders[i] = ctx;
                memset(&s_slots[i], 0, sizeof(s_slots[i]));
                break;
            }
        }
    }
    fmrb_semaphore_give(s_lock);
    return ok;
}

/* Drop ctx's use of the runtime and end the runtime with the last one; the
   caller holds s_lock. false when ctx was not a user. */
static bool runtime_drop_locked(fmrb_app_task_context_t *ctx)
{
    int i = 0;
    while (i < FMRB_MAX_APPS && s_holders[i] != ctx) i++;
    if (i == FMRB_MAX_APPS) return false;
    s_holders[i] = NULL;
    s_slots[i].inst = NULL;
    if (--s_users == 0) {
        mem_alloc_info_t info;
        bool have = wasm_runtime_get_mem_alloc_info(&info);
        if (have && info.highmark_size > s_pool_high) {
            s_pool_high = (uint32_t)info.highmark_size;
        }
        wasm_runtime_unregister_natives("fmrb", s_natives);
        wasm_runtime_free(s_natives);
        s_natives = NULL;
        wasm_runtime_destroy();
        if (have) {
            FMRB_LOGI(TAG, "runtime ended (pool high mark %u of %u bytes)",
                      (unsigned)info.highmark_size, (unsigned)info.total_size);
        } else {
            FMRB_LOGI(TAG, "runtime ended");
        }
    }
    return true;
}

static void runtime_release(fmrb_app_task_context_t *ctx)
{
    fmrb_semaphore_take(s_lock, portMAX_DELAY);
    runtime_drop_locked(ctx);
    fmrb_semaphore_give(s_lock);
}

void fmrb_wasm_app_forget(fmrb_app_task_context_t *ctx)
{
    /* The app's task is gone, inside a call or between them. Nothing of its
       instance is freed here: the deleted task may have held the shared
       pool's allocator lock or the app pool's, and the linear memory and the
       binary copy go with the app pool anyway. Its module, instance and exec
       env stay in the shared pool until the runtime ends; when it was the
       last wasm app, that is now, and wasm_runtime_destroy drops the whole
       pool (the next start lays it out afresh). */
    if (!s_lock) return;
    if (fmrb_semaphore_take(s_lock, FMRB_MS_TO_TICKS(FMRB_WASM_LOCK_WAIT_MS)) != pdTRUE) {
        FMRB_LOGE(TAG, "[%s] killed while holding the runtime's lock: wasm apps cannot start "
                  "until a reboot", ctx->app_name);
        return;
    }
    if (runtime_drop_locked(ctx)) {
        FMRB_LOGW(TAG, "[%s] killed by force: its use of the runtime is given back "
                  "(%d wasm app%s left)", ctx->app_name, s_users, s_users == 1 ? "" : "s");
    }
    fmrb_semaphore_give(s_lock);
}

/* The lowest address the interpreter may use of the calling task's stack. */
static uint8_t *native_stack_boundary(void)
{
#if !defined(CONFIG_IDF_TARGET_LINUX)
    uint8_t *low = (uint8_t *)pxTaskGetStackStart(NULL);
    return low ? low + FMRB_WASM_NATIVE_STACK_GUARD : NULL;
#elif !defined(__EMSCRIPTEN__)
    pthread_attr_t attr;
    void *addr = NULL;
    size_t size = 0;
    if (pthread_getattr_np(pthread_self(), &attr) != 0) return NULL;
    pthread_attr_getstack(&attr, &addr, &size);
    pthread_attr_destroy(&attr);
    return addr ? (uint8_t *)addr + FMRB_WASM_NATIVE_STACK_GUARD : NULL;
#else
    /* The browser build: every FreeRTOS task is a pthread (a Worker) whose C
       stack is in emscripten's linear memory, and emscripten keeps the
       calling thread's limits (doc/wasm_apps report/w1c.md). */
    uintptr_t end = emscripten_stack_get_end();
    return end ? (uint8_t *)end + FMRB_WASM_NATIVE_STACK_GUARD : NULL;
#endif
}

/* ------------------------------------------------------------ the app */

typedef struct {
    fmrb_app_task_context_t *ctx;
    wasm_slot_t *slot;          /* its row of s_slots (NULL: not found) */
    wasm_module_inst_t inst;
    wasm_exec_env_t env;
    wasm_function_inst_t fn[FMRB_SHELL_HOOK_COUNT];
    int32_t drawer;
    uint32_t *sp;               /* the app's stack pointer global */
    wasm_function_inst_t buf_fn; /* the SDK's fmrb_event_buffer, when exported */
    uint32_t buf;               /* the address it gave (0: not asked yet) */
    char *err;
    size_t err_cap;
} wasm_app_t;

static const char *const HOOK_NAMES[FMRB_SHELL_HOOK_COUNT] = {
    [FMRB_SHELL_HOOK_LOAD]         = "_initialize",
    [FMRB_SHELL_HOOK_CREATE]       = "on_create",
    [FMRB_SHELL_HOOK_UPDATE]       = "on_update",
    [FMRB_SHELL_HOOK_EVENT]        = "on_event",
    [FMRB_SHELL_HOOK_DESTROY]      = "on_destroy",
    [FMRB_SHELL_HOOK_SUSPEND]      = "on_suspend",
    [FMRB_SHELL_HOOK_RESUME]       = "on_resume",
    [FMRB_SHELL_HOOK_RESIZE]       = "on_resize",
    [FMRB_SHELL_HOOK_QUIT_REQUEST] = "on_quit_request",
};

/* Parameters and results each hook must have. */
static const uint8_t HOOK_PARAMS[FMRB_SHELL_HOOK_COUNT] = {
    [FMRB_SHELL_HOOK_EVENT] = 1, [FMRB_SHELL_HOOK_RESIZE] = 2,
};
static const uint8_t HOOK_RESULTS[FMRB_SHELL_HOOK_COUNT] = {
    [FMRB_SHELL_HOOK_UPDATE] = 1,
};

/* Call a hook; argv holds the arguments and gets the result. */
static int call_hook(wasm_app_t *a, fmrb_shell_hook_t hook, uint32_t argc, uint32_t *argv)
{
    wasm_function_inst_t f = a->fn[hook];
    if (!f) return FMRB_SHELL_OK;
    wasm_runtime_set_instruction_count_limit(a->env, FMRB_WASM_CALL_INSN_LIMIT);
    uint64_t t0 = fmrb_hal_time_get_us();
    bool ok = wasm_runtime_call_wasm(a->env, f, argc, argv);
    if (a->slot) {
        uint32_t us = (uint32_t)(fmrb_hal_time_get_us() - t0);
        a->slot->hook_us = us;
        if (us > a->slot->hook_max_us) a->slot->hook_max_us = us;
        a->slot->calls++;
    }
    if (ok) {
        return FMRB_SHELL_OK;
    }
    /* Counted under the runtime's lock, not with an atomic add: s_traps is in
       PSRAM, and on the S3 an atomic on PSRAM is a libcall into newlib's
       stdatomic, which ESP-IDF keeps in internal RAM (IRAM). */
    fmrb_semaphore_take(s_lock, portMAX_DELAY);
    s_traps++;
    fmrb_semaphore_give(s_lock);
    const char *exc = wasm_runtime_get_exception(a->inst);
    if (a->err && a->err_cap) {
        snprintf(a->err, a->err_cap, "%s: %s", HOOK_NAMES[hook], exc ? exc : "trap");
    }
    FMRB_LOGE(TAG, "[%s] %s stopped: %s", a->ctx->app_name, HOOK_NAMES[hook],
              exc ? exc : "trap");
    return FMRB_SHELL_RAISED;
}

static bool e_has_hook(void *ud, fmrb_shell_hook_t hook)
{
    wasm_app_t *a = ud;
    return a->fn[hook] != NULL;
}

static int e_call(void *ud, fmrb_shell_hook_t hook)
{
    return call_hook(ud, hook, 0, NULL);
}

static int e_call_update(void *ud, int *next_ms)
{
    wasm_app_t *a = ud;
    uint32_t argv[1] = { 0 };
    *next_ms = -1;
    int rc = call_hook(a, FMRB_SHELL_HOOK_UPDATE, 0, argv);
    if (rc == FMRB_SHELL_OK && a->fn[FMRB_SHELL_HOOK_UPDATE]) {
        /* A negative result asks for the default interval, as a Lua
           on_update that returns no number does. */
        int32_t ms = (int32_t)argv[0];
        *next_ms = ms < 0 ? -1 : ms;
    }
    return rc;
}

static int e_call_event(void *ud, const fmrb_shell_event_t *ev)
{
    wasm_app_t *a = ud;
    if (!a->fn[FMRB_SHELL_HOOK_EVENT]) return FMRB_SHELL_OK;
    fmrb_bnd_app_event_t r;
    memset(&r, 0, sizeof(r));
    switch (ev->kind) {
    case FMRB_HID_EVENT_KEY_DOWN:
    case FMRB_HID_EVENT_KEY_UP:
        r.type = ev->kind == FMRB_HID_EVENT_KEY_DOWN ? FMRB_BND_APP_EVENT_TYPE_KEY_DOWN
                                                     : FMRB_BND_APP_EVENT_TYPE_KEY_UP;
        r.keycode = ev->keycode; r.scancode = ev->scancode;
        r.modifier = ev->modifier; r.character = ev->character;
        break;
    case FMRB_HID_EVENT_MOUSE_DOWN:
    case FMRB_HID_EVENT_MOUSE_UP:
    case FMRB_HID_EVENT_MOUSE_MOVE:
    case FMRB_HID_EVENT_MOUSE_WHEEL:
        r.type = ev->kind == FMRB_HID_EVENT_MOUSE_DOWN ? FMRB_BND_APP_EVENT_TYPE_MOUSE_DOWN
               : ev->kind == FMRB_HID_EVENT_MOUSE_UP   ? FMRB_BND_APP_EVENT_TYPE_MOUSE_UP
               : ev->kind == FMRB_HID_EVENT_MOUSE_MOVE ? FMRB_BND_APP_EVENT_TYPE_MOUSE_MOVE
                                                       : FMRB_BND_APP_EVENT_TYPE_MOUSE_WHEEL;
        if (ev->kind == FMRB_HID_EVENT_MOUSE_DOWN || ev->kind == FMRB_HID_EVENT_MOUSE_UP) {
            r.button = ev->button;
        }
        if (ev->kind == FMRB_HID_EVENT_MOUSE_WHEEL) r.delta = ev->delta;
        r.x = ev->x; r.y = ev->y; r.wx = ev->wx; r.wy = ev->wy;
        break;
    case FMRB_HID_EVENT_GAMEPAD_DOWN:
    case FMRB_HID_EVENT_GAMEPAD_UP:
        r.type = ev->kind == FMRB_HID_EVENT_GAMEPAD_DOWN ? FMRB_BND_APP_EVENT_TYPE_GAMEPAD_DOWN
                                                         : FMRB_BND_APP_EVENT_TYPE_GAMEPAD_UP;
        r.gamepad_id = ev->gamepad_id; r.button = ev->button;
        break;
    case FMRB_HID_EVENT_GAMEPAD_AXIS:
        r.type = FMRB_BND_APP_EVENT_TYPE_GAMEPAD_AXIS;
        r.gamepad_id = ev->gamepad_id; r.axis = ev->axis; r.value = ev->value;
        break;
    case FMRB_HID_EVENT_KANA_MODE:
        r.type = FMRB_BND_APP_EVENT_TYPE_KANA_MODE;
        r.mode = ev->mode;
        break;
    default:
        return FMRB_SHELL_OK;
    }

    if (a->buf_fn && !a->buf) {
        /* Ask once where the SDK's buffer is. */
        uint32_t argv[1] = { 0 };
        wasm_runtime_set_instruction_count_limit(a->env, FMRB_WASM_CALL_INSN_LIMIT);
        bool called = wasm_runtime_call_wasm(a->env, a->buf_fn, 0, argv);
        if (called && argv[0] != 0 &&
            wasm_runtime_validate_app_addr(a->inst, argv[0], FMRB_BND_EVENT_BUFFER_SIZE)) {
            a->buf = argv[0];
        } else {
            wasm_runtime_clear_exception(a->inst);
            a->buf_fn = NULL;
            FMRB_LOGW(TAG, "[%s] %s gave no usable buffer (0x%x)%s", a->ctx->app_name,
                      FMRB_BND_EVENT_BUFFER_EXPORT, (unsigned)argv[0],
                      a->sp ? ": the record goes on the app's stack" : "");
            if (!a->sp) {
                if (a->err && a->err_cap) {
                    snprintf(a->err, a->err_cap, "on_event: %s gave no usable buffer",
                             FMRB_BND_EVENT_BUFFER_EXPORT);
                }
                return FMRB_SHELL_RAISED;
            }
        }
    }
    if (a->buf) {
        memcpy(wasm_runtime_addr_app_to_native(a->inst, a->buf), &r, sizeof(r));
        uint32_t argv[1] = { a->buf };
        return call_hook(a, FMRB_SHELL_HOOK_EVENT, 1, argv);
    }

    /* No buffer: the record goes on the app's C stack, 16-byte aligned below
       its stack pointer, which stays lowered over the call. */
    uint32_t sp = *a->sp;
    uint32_t at = (sp - (uint32_t)sizeof(r)) & ~15u;
    if (sp < sizeof(r) + 16 ||
        !wasm_runtime_validate_app_addr(a->inst, at, sizeof(r))) {
        wasm_runtime_clear_exception(a->inst);   /* the check raises one */
        if (a->err && a->err_cap) {
            snprintf(a->err, a->err_cap, "on_event: no room on the app's stack (sp 0x%x)",
                     (unsigned)sp);
        }
        FMRB_LOGE(TAG, "[%s] on_event: no room on the app's stack (sp 0x%x)",
                  a->ctx->app_name, (unsigned)sp);
        return FMRB_SHELL_RAISED;
    }
    memcpy(wasm_runtime_addr_app_to_native(a->inst, at), &r, sizeof(r));
    *a->sp = at;
    uint32_t argv[1] = { at };
    int rc = call_hook(a, FMRB_SHELL_HOOK_EVENT, 1, argv);
    *a->sp = sp;
    return rc;
}

static int e_call_resize(void *ud, int w, int h)
{
    uint32_t argv[2] = { (uint32_t)w, (uint32_t)h };
    return call_hook(ud, FMRB_SHELL_HOOK_RESIZE, 2, argv);
}

static void e_layout(void *ud, const fmrb_shell_layout_t *l)
{
    wasm_app_t *a = ud;
    if (l->canvas < 0) return;   /* headless: no drawer */
    if (a->drawer < 0) {
        a->drawer = fmrb_gfx_drawer_open(l->canvas, l->area_w, l->area_h);
        if (a->drawer != 0) {
            FMRB_LOGW(TAG, "[%s] the window's drawer is %d, not 0", a->ctx->app_name,
                      (int)a->drawer);
        }
    }
    fmrb_gfx_drawer_set_canvas(a->drawer, l->canvas);
    fmrb_gfx_drawer_set_origin(a->drawer, l->area_x, l->area_y, l->area_w, l->area_h);
}

static const fmrb_shell_engine_t WASM_ENGINE = {
    .has_hook = e_has_hook,
    .call = e_call,
    .call_update = e_call_update,
    .call_event = e_call_event,
    .call_resize = e_call_resize,
    .layout = e_layout,
};

/* Read the file into the app's pool. */
static uint8_t *read_binary(fmrb_app_task_context_t *ctx, const char *path, uint32_t *size,
                            char *err, size_t err_cap)
{
    fmrb_file_t f = NULL;
    if (fmrb_hal_file_open(path, FMRB_O_RDONLY, &f) != FMRB_OK) {
        snprintf(err, err_cap, "cannot open %s", path);
        return NULL;
    }
    uint32_t n = 0;
    uint8_t *buf = NULL;
    if (fmrb_hal_file_size(f, &n) != FMRB_OK || n == 0) {
        snprintf(err, err_cap, "cannot read the size of %s", path);
    } else if (!(buf = fmrb_malloc(ctx->mem_handle, n))) {
        snprintf(err, err_cap, "no room for %u bytes of %s in the app's pool", (unsigned)n, path);
    } else {
        size_t got = 0, total = 0;
        while (total < n &&
               fmrb_hal_file_read(f, buf + total, n - total, &got) == FMRB_OK && got > 0) {
            total += got;
        }
        if (total != n) {
            snprintf(err, err_cap, "short read of %s (%u of %u bytes)", path,
                     (unsigned)total, (unsigned)n);
            fmrb_free(ctx->mem_handle, buf);
            buf = NULL;
        }
    }
    fmrb_hal_file_close(f);
    *size = n;
    return buf;
}

/* The SDK version the app was built with: the content of the module's
   custom section "fmrb.sdk" (each SDK language's library puts its version
   there). Read from the binary before WAMR loads it (the loader may work on
   the buffer in place); "" when there is none or it is not printable. */
#define SDK_SECTION_NAME "fmrb.sdk"

static bool read_leb_u32(const uint8_t *p, const uint8_t *end, const uint8_t **next, uint32_t *v)
{
    uint32_t r = 0;
    for (int shift = 0; shift < 35 && p < end; shift += 7) {
        uint8_t b = *p++;
        r |= (uint32_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) {
            *v = r;
            *next = p;
            return true;
        }
    }
    return false;
}

static void read_sdk_version(const uint8_t *bin, uint32_t size, char *out, size_t cap)
{
    out[0] = '\0';
    if (size < 8 || memcmp(bin, "\0asm", 4) != 0) return;
    const uint8_t *p = bin + 8, *end = bin + size;
    while (p < end) {
        uint8_t id = *p++;
        uint32_t len, nlen;
        if (!read_leb_u32(p, end, &p, &len) || len > (uint32_t)(end - p)) return;
        const uint8_t *sec_end = p + len;
        if (id == 0 && read_leb_u32(p, sec_end, &p, &nlen) && nlen <= (uint32_t)(sec_end - p) &&
            nlen == sizeof(SDK_SECTION_NAME) - 1 && memcmp(p, SDK_SECTION_NAME, nlen) == 0) {
            p += nlen;
            size_t n = (size_t)(sec_end - p);
            if (n == 0 || n >= cap) return;
            for (size_t i = 0; i < n; i++) {
                if (p[i] < 0x21 || p[i] > 0x7e) return;
            }
            memcpy(out, p, n);
            out[n] = '\0';
            return;
        }
        p = sec_end;
    }
}

/* The refusal of an app whose function imports are not all linked: the
   first such import (module, name), how many there are, and the SDK
   version the app carries. Shared by the check at load (check_imports) and
   the one an installer makes (fmrb_wasm_check_file). */
static void refuse_imports(const char *module_name, const char *name, int32_t missing, const char *sdk_ver,
                           char *err, size_t err_cap)
{
    char more[24] = "";
    if (missing > 1) snprintf(more, sizeof(more), " (and %d more)", (int)(missing - 1));
    if (strcmp(module_name, "fmrb") != 0) {
        /* WASI or another runtime's functions: no firmware has them. */
        snprintf(err, err_cap, "imports %.32s.%.48s%s, but the firmware gives only \"fmrb\" "
                 "functions: build the app without WASI or libc's I/O", module_name, name, more);
        return;
    }
    const char *what = "which this firmware does not have";
    for (size_t i = 0; i < NATIVES_COUNT; i++) {
        if (strcmp(k_natives[i].symbol, name) == 0) {
            what = "in another shape than this firmware's";
            break;
        }
    }
    snprintf(err, err_cap, "imports fmrb.%.48s, %s%s: the app was built with %s%s%s; "
             "update the firmware", name, what, more,
             sdk_ver[0] ? "SDK " : "a newer SDK", sdk_ver, sdk_ver[0] ? ", newer than this firmware" : "");
}

/* Every function the module imports must be one of the boundary's with the
   boundary's shape (doc/wasm_apps decision 14). WAMR links an import when
   its module, name and signature match a registered native, and leaves the
   others to trap when called; an app made with a newer SDK than this
   firmware would start and then stop at the first call of what is missing.
   This refuses it at load, with the first such import and the SDK version
   the app carries (sdk_ver, "" when none). Imports that are not functions
   are left to WAMR. */
static bool check_imports(wasm_module_t module, const char *sdk_ver, char *err, size_t err_cap)
{
    int32_t count = wasm_runtime_get_import_count(module);
    int32_t missing = 0;
    wasm_import_t first;
    memset(&first, 0, sizeof(first));
    for (int32_t i = 0; i < count; i++) {
        wasm_import_t im;
        wasm_runtime_get_import_type(module, i, &im);
        if (im.kind != WASM_IMPORT_EXPORT_KIND_FUNC || im.linked) continue;
        if (missing++ == 0) first = im;
    }
    if (missing == 0) return true;
    refuse_imports(first.module_name, first.name, missing, sdk_ver, err, err_cap);
    return false;
}

/* ---- the check without WAMR (an installer's, fmrb_wasm_check_file) ---- */

/* The file read a little at a time. */
typedef struct {
    fmrb_file_t f;
    uint32_t pos;           /* of the next byte in the file */
    uint32_t size;
    uint8_t buf[64];
    uint32_t have, at;      /* bytes in buf, the next one */
    bool bad;
} wreader_t;

static int wr_byte(wreader_t *r)
{
    if (r->at == r->have) {
        size_t got = 0;
        if (r->bad || r->pos >= r->size ||
            fmrb_hal_file_read(r->f, r->buf, sizeof(r->buf), &got) != FMRB_OK || got == 0) {
            r->bad = true;
            return -1;
        }
        r->have = (uint32_t)got;
        r->at = 0;
    }
    r->pos++;
    return r->buf[r->at++];
}

static bool wr_leb(wreader_t *r, uint32_t *v)
{
    uint32_t out = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        int b = wr_byte(r);
        if (b < 0) return false;
        out |= (uint32_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) {
            *v = out;
            return true;
        }
    }
    return false;
}

static bool wr_read(wreader_t *r, uint8_t *dst, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        int b = wr_byte(r);
        if (b < 0) return false;
        dst[i] = (uint8_t)b;
    }
    return true;
}

static bool wr_skip(wreader_t *r, uint32_t n)
{
    uint32_t left = r->have - r->at;
    if (n <= left) {
        r->at += n;
        r->pos += n;
        return true;
    }
    if (n > r->size - r->pos) {
        r->bad = true;
        return false;
    }
    r->pos += n;
    r->at = r->have = 0;
    return fmrb_hal_file_seek(r->f, (int32_t)r->pos, FMRB_SEEK_SET) == FMRB_OK;
}

/* A section held in memory, read with bounds. */
typedef struct {
    const uint8_t *p, *end;
} wbytes_t;

static bool wb_leb(wbytes_t *b, uint32_t *v)
{
    return read_leb_u32(b->p, b->end, &b->p, v);
}

static bool wb_name(wbytes_t *b, const uint8_t **s, uint32_t *n)
{
    if (!wb_leb(b, n) || *n > (uint32_t)(b->end - b->p)) return false;
    *s = b->p;
    b->p += *n;
    return true;
}

/* WAMR's signature check (wasm_native.c check_symbol_signature) for a
   function type of params and results (value type bytes): i i32, I i64,
   f f32, F f64; * (a pointer, maybe followed by ~ its length) and $ (a
   string) take an i32. Only the first result is looked at, as WAMR does. */
static bool sig_type_is(uint8_t t, char c)
{
    return (t == 0x7f && c == 'i') || (t == 0x7e && c == 'I') || (t == 0x7d && c == 'f') ||
           (t == 0x7c && c == 'F');
}

static bool sig_matches(const char *sig, const uint8_t *params, uint32_t np, const uint8_t *res, uint32_t nr)
{
    if (!sig || sig[0] == '\0') return true;
    const char *p = sig, *end = sig + strlen(sig);
    if (end - p < 2 || *p++ != '(') return false;
    if ((uint32_t)(end - p) < np + 1) return false;
    for (uint32_t i = 0; i < np; i++) {
        char c = *p++;
        if (sig_type_is(params[i], c)) continue;
        if (params[i] != 0x7f) return false;
        if (c == '*') {
            if (i + 1 < np && params[i + 1] == 0x7f && *p == '~') {
                i++;
                p++;
            }
        } else if (c != '$') {
            return false;
        }
    }
    if (*p++ != ')') return false;
    if (nr) {
        if (p >= end || !sig_type_is(res[0], *p)) return false;
        p++;
    }
    return *p == '\0';
}

/* The boundary's native of that name (WAMR also tries it without a leading
   _), NULL when none. */
static const NativeSymbol *native_named(const uint8_t *name, uint32_t n)
{
    for (int pass = 0; pass < 2; pass++) {
        const uint8_t *s = name;
        uint32_t len = n;
        if (pass == 1) {
            if (n == 0 || name[0] != '_') break;
            s++;
            len--;
        }
        for (size_t i = 0; i < NATIVES_COUNT; i++) {
            const char *sym = k_natives[i].symbol;
            if (strlen(sym) == len && memcmp(sym, s, len) == 0) return &k_natives[i];
        }
    }
    return NULL;
}

/* One import section against the type section: false (and err) at the
   first function import this firmware does not link. */
static bool check_import_section(wbytes_t types, uint32_t *offs, uint32_t ntypes, wbytes_t im,
                                 const char *sdk_ver, char *err, size_t err_cap)
{
    uint32_t count;
    if (!wb_leb(&im, &count)) goto bad;
    int32_t missing = 0;
    char first_mod[33] = "", first_name[49] = "";
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *mod, *name;
        uint32_t mn, nn, idx, v;
        if (!wb_name(&im, &mod, &mn) || !wb_name(&im, &name, &nn) || im.p >= im.end) goto bad;
        uint8_t kind = *im.p++;
        if (kind == 0) {
            if (!wb_leb(&im, &idx) || idx >= ntypes) goto bad;
        } else if (kind == 1) {           /* table: reftype, limits */
            if (im.p >= im.end) goto bad;
            im.p++;
            uint32_t flags;
            if (!wb_leb(&im, &flags) || !wb_leb(&im, &v) || ((flags & 1) && !wb_leb(&im, &v))) goto bad;
            continue;
        } else if (kind == 2) {           /* memory: limits */
            uint32_t flags;
            if (!wb_leb(&im, &flags) || !wb_leb(&im, &v) || ((flags & 1) && !wb_leb(&im, &v))) goto bad;
            continue;
        } else if (kind == 3) {           /* global: type, mutability */
            if (im.end - im.p < 2) goto bad;
            im.p += 2;
            continue;
        } else if (kind == 4) {           /* tag: attribute, type */
            if (im.p >= im.end) goto bad;
            im.p++;
            if (!wb_leb(&im, &v)) goto bad;
            continue;
        } else {
            goto bad;
        }
        /* the function's type: 0x60, params, results */
        wbytes_t t = { types.p + offs[idx], types.end };
        uint32_t np, nr;
        if (t.p >= t.end || *t.p++ != 0x60 || !wb_leb(&t, &np) || np > (uint32_t)(t.end - t.p)) goto bad;
        const uint8_t *params = t.p;
        t.p += np;
        if (!wb_leb(&t, &nr) || nr > (uint32_t)(t.end - t.p)) goto bad;
        const uint8_t *res = t.p;
        bool linked = false;
        if (mn == 4 && memcmp(mod, "fmrb", 4) == 0) {
            const NativeSymbol *ns = native_named(name, nn);
            linked = ns && sig_matches(ns->signature, params, np, res, nr);
        }
        if (!linked && missing++ == 0) {
            snprintf(first_mod, sizeof(first_mod), "%.*s", (int)(mn < 32 ? mn : 32), (const char *)mod);
            snprintf(first_name, sizeof(first_name), "%.*s", (int)(nn < 48 ? nn : 48), (const char *)name);
        }
    }
    if (missing == 0) return true;
    refuse_imports(first_mod, first_name, missing, sdk_ver, err, err_cap);
    return false;
bad:
    snprintf(err, err_cap, "its import section cannot be read (not a wasm module this firmware loads)");
    return false;
}

/* Where each function type starts in the type section (offsets from its
   start, after the count); false when it cannot be read. */
static bool index_types(wbytes_t types, uint32_t *offs, uint32_t ntypes)
{
    const uint8_t *start = types.p;
    for (uint32_t i = 0; i < ntypes; i++) {
        offs[i] = (uint32_t)(types.p - start);
        uint32_t n;
        if (types.p >= types.end || *types.p++ != 0x60) return false;
        for (int k = 0; k < 2; k++) {
            if (!wb_leb(&types, &n) || n > (uint32_t)(types.end - types.p)) return false;
            types.p += n;
        }
    }
    return true;
}

bool fmrb_wasm_check_file(const char *path, fmrb_wasm_alloc_fn alloc, fmrb_wasm_free_fn release, void *ud,
                          char *sdk_ver, size_t sdk_cap, char *err, size_t err_cap)
{
    if (sdk_cap) sdk_ver[0] = '\0';
    if (err_cap) err[0] = '\0';
    wreader_t r;
    memset(&r, 0, sizeof(r));
    if (fmrb_hal_file_open(path, FMRB_O_RDONLY, &r.f) != FMRB_OK) {
        snprintf(err, err_cap, "cannot open %s", path);
        return false;
    }
    bool ok = false;
    uint8_t *types = NULL, *imports = NULL;
    uint32_t *offs = NULL;
    uint32_t types_len = 0, imports_len = 0;
    uint8_t head[8];
    if (fmrb_hal_file_size(r.f, &r.size) != FMRB_OK || !wr_read(&r, head, 8) ||
        memcmp(head, "\0asm", 4) != 0 || head[4] != 1) {
        snprintf(err, err_cap, "%s is not a wasm module", path);
        goto out;
    }
    while (r.pos < r.size) {
        int id = wr_byte(&r);
        uint32_t len;
        if (id < 0 || !wr_leb(&r, &len) || len > r.size - r.pos) {
            snprintf(err, err_cap, "%s: a section goes past the end of the file", path);
            goto out;
        }
        uint8_t **into = id == 1 ? &types : id == 2 ? &imports : NULL;
        if (into && !*into) {
            *into = alloc(ud, len ? len : 1);
            if (!*into) {
                snprintf(err, err_cap, "no room for %u bytes of %s's sections", (unsigned)len, path);
                goto out;
            }
            if (!wr_read(&r, *into, len)) {
                snprintf(err, err_cap, "short read of %s", path);
                goto out;
            }
            if (id == 1) types_len = len;
            else imports_len = len;
            continue;
        }
        if (id == 0 && sdk_cap && !sdk_ver[0]) {
            /* a custom section: fmrb.sdk holds the SDK version (read_sdk_version) */
            uint32_t at = r.pos, nlen;
            char name[sizeof(SDK_SECTION_NAME)];
            if (wr_leb(&r, &nlen) && nlen == sizeof(SDK_SECTION_NAME) - 1 &&
                wr_read(&r, (uint8_t *)name, nlen) && memcmp(name, SDK_SECTION_NAME, nlen) == 0) {
                uint32_t n = len - (r.pos - at);
                if (n > 0 && n < sdk_cap) {
                    char v[32];
                    if (n < sizeof(v) && wr_read(&r, (uint8_t *)v, n)) {
                        bool printable = true;
                        for (uint32_t i = 0; i < n; i++) printable = printable && v[i] >= 0x21 && v[i] <= 0x7e;
                        if (printable) {
                            memcpy(sdk_ver, v, n);
                            sdk_ver[n] = '\0';
                        }
                    }
                }
            }
            if (r.bad || r.pos > at + len) {
                snprintf(err, err_cap, "%s: a custom section cannot be read", path);
                goto out;
            }
            if (!wr_skip(&r, at + len - r.pos)) goto short_read;
            continue;
        }
        if (!wr_skip(&r, len)) goto short_read;
    }
    if (!imports) {
        ok = true;          /* imports nothing */
        goto out;
    }
    uint32_t ntypes = 0;
    wbytes_t tb = { types, types + types_len };
    if (types && !wb_leb(&tb, &ntypes)) ntypes = 0;
    if (ntypes > types_len) {
        snprintf(err, err_cap, "%s: its type section cannot be read", path);
        goto out;
    }
    if (ntypes) {
        offs = alloc(ud, ntypes * sizeof(uint32_t));
        if (!offs) {
            snprintf(err, err_cap, "no room for the types of %s", path);
            goto out;
        }
        if (!index_types(tb, offs, ntypes)) {
            snprintf(err, err_cap, "%s: its type section cannot be read", path);
            goto out;
        }
    }
    wbytes_t ib = { imports, imports + imports_len };
    ok = check_import_section(tb, offs, ntypes, ib, sdk_ver, err, err_cap);
    goto out;
short_read:
    snprintf(err, err_cap, "short read of %s", path);
out:
    if (offs) release(ud, offs);
    if (types) release(ud, types);
    if (imports) release(ud, imports);
    fmrb_hal_file_close(r.f);
    return ok;
}

/* The app's stack pointer: the first mutable i32 global the module
   defines (LLVM's __stack_pointer; WAMR finds it the same way when the
   module exports __heap_base, which these apps do not). */
static uint32_t *stack_pointer_global(wasm_module_inst_t inst)
{
    WASMModuleInstance *mi = (WASMModuleInstance *)inst;
    if (mi->module_type != Wasm_Module_Bytecode) return NULL;
    for (uint32_t i = mi->module->import_global_count; i < mi->e->global_count; i++) {
        WASMGlobalInstance *g = &mi->e->globals[i];
        if (g->is_mutable && g->type == VALUE_TYPE_I32) {
            return (uint32_t *)(mi->global_data + g->data_offset);
        }
    }
    return NULL;
}

/* Look up the hooks and check their types; false (with why) when one has
   the wrong shape. */
static bool find_hooks(wasm_app_t *a, char *err, size_t err_cap)
{
    for (int h = 0; h < FMRB_SHELL_HOOK_COUNT; h++) {
        wasm_function_inst_t f = wasm_runtime_lookup_function(a->inst, HOOK_NAMES[h]);
        if (!f) continue;
        uint32_t np = wasm_func_get_param_count(f, a->inst);
        uint32_t nr = wasm_func_get_result_count(f, a->inst);
        if (np != HOOK_PARAMS[h] || nr != HOOK_RESULTS[h]) {
            snprintf(err, err_cap, "%s takes %u and returns %u values, expected %u and %u",
                     HOOK_NAMES[h], (unsigned)np, (unsigned)nr,
                     (unsigned)HOOK_PARAMS[h], (unsigned)HOOK_RESULTS[h]);
            return false;
        }
        a->fn[h] = f;
    }
    if (!a->fn[FMRB_SHELL_HOOK_EVENT]) return true;
    wasm_function_inst_t buf = wasm_runtime_lookup_function(a->inst, FMRB_BND_EVENT_BUFFER_EXPORT);
    if (buf) {
        if (wasm_func_get_param_count(buf, a->inst) != 0 ||
            wasm_func_get_result_count(buf, a->inst) != 1) {
            snprintf(err, err_cap, "%s must take nothing and return an address",
                     FMRB_BND_EVENT_BUFFER_EXPORT);
            return false;
        }
        a->buf_fn = buf;
    }
    a->sp = stack_pointer_global(a->inst);
    if (!a->buf_fn && !a->sp) {
        snprintf(err, err_cap, "on_event needs %s (the SDK's event buffer) or a stack pointer "
                 "global (__stack_pointer) for its record: rebuild with the current SDK",
                 FMRB_BND_EVENT_BUFFER_EXPORT);
        return false;
    }
    return true;
}

/* Show the app's instance in its row (fmrb_wasm_app_stats), and take it out
   again before the instance goes. */
static void slot_attach(wasm_app_t *a, uint32_t binary_size, uint32_t max_pages)
{
    fmrb_semaphore_take(s_lock, portMAX_DELAY);
    for (int i = 0; i < FMRB_MAX_APPS; i++) {
        if (s_holders[i] == a->ctx) {
            a->slot = &s_slots[i];
            a->slot->binary_size = binary_size;
            a->slot->max_pages = max_pages;
            a->slot->inst = a->inst;
            break;
        }
    }
    fmrb_semaphore_give(s_lock);
}

static void slot_detach(wasm_app_t *a)
{
    if (!a->slot) return;
    fmrb_semaphore_take(s_lock, portMAX_DELAY);
    a->slot->inst = NULL;
    fmrb_semaphore_give(s_lock);
    a->slot = NULL;
}

fmrb_err_t fmrb_wasm_app_run(fmrb_app_task_context_t *ctx, const char *path,
                             char *err, size_t err_cap)
{
    char why[160];
    if (err && err_cap) err[0] = '\0';
    if (!runtime_acquire(ctx, err, err_cap)) {
        FMRB_LOGE(TAG, "[%s] %s", ctx->app_name, err);
        return FMRB_ERR_FAILED;
    }

    fmrb_err_t ret = FMRB_ERR_FAILED;
    wasm_module_t module = NULL;
    wasm_app_t a = { .ctx = ctx, .drawer = -1, .err = err, .err_cap = err_cap };
    uint32_t size = 0;
    uint8_t *bin = read_binary(ctx, path, &size, err, err_cap);
    if (!bin) goto out;

    char sdk_ver[24];
    read_sdk_version(bin, size, sdk_ver, sizeof(sdk_ver));
    char lerr[128];
    module = wasm_runtime_load(bin, size, lerr, sizeof(lerr));
    if (!module) {
        snprintf(err, err_cap, "%s did not load: %s", path, lerr);
        goto out;
    }
    if (!check_imports(module, sdk_ver, err, err_cap)) goto out;

    /* The linear memory may grow to what the app's pool has left. */
    fmrb_pool_stats_t st;
    uint32_t max_pages = 0;
    if (fmrb_mem_get_stats(ctx->mem_handle, &st) == 0 &&
        st.free_size > FMRB_WASM_POOL_MARGIN + WASM_PAGE_SIZE) {
        max_pages = (uint32_t)((st.free_size - FMRB_WASM_POOL_MARGIN) / WASM_PAGE_SIZE);
    } else {
        max_pages = 1;
    }
    struct InstantiationArgs2 *args = NULL;
    if (!wasm_runtime_instantiation_args_create(&args)) {
        snprintf(err, err_cap, "wasm: no room for the instantiation arguments");
        goto out;
    }
    wasm_runtime_instantiation_args_set_default_stack_size(args, FMRB_WASM_OPERAND_STACK);
    wasm_runtime_instantiation_args_set_host_managed_heap_size(args, 0);
    wasm_runtime_instantiation_args_set_max_memory_pages(args, max_pages);
    a.inst = wasm_runtime_instantiate_ex2(module, args, lerr, sizeof(lerr));
    wasm_runtime_instantiation_args_destroy(args);
    if (!a.inst) {
        snprintf(err, err_cap, "%s did not start: %s", path, lerr);
        goto out;
    }
    wasm_runtime_set_custom_data(a.inst, ctx);
    a.env = wasm_runtime_create_exec_env(a.inst, FMRB_WASM_OPERAND_STACK);
    if (!a.env) {
        snprintf(err, err_cap, "wasm: no room for the exec env");
        goto out;
    }
    uint8_t *bound = native_stack_boundary();
    wasm_runtime_set_native_stack_boundary(a.env, bound);
    /* What the interpreter and the boundary functions have below this frame. */
    unsigned stack_room = bound ? (unsigned)((uint8_t *)&bound - bound) : 0u;
    if (!find_hooks(&a, err, err_cap)) goto out;

    wasm_memory_inst_t mem = wasm_runtime_get_default_memory(a.inst);
    slot_attach(&a, size, max_pages);
    mem_alloc_info_t pinfo;
    unsigned pool_used = wasm_runtime_get_mem_alloc_info(&pinfo)
                             ? (unsigned)(pinfo.total_size - pinfo.total_free_size) : 0u;
    FMRB_LOGI(TAG, "[%s] loaded %s (%u bytes, memory %u pages, up to %u, shared pool used %u, "
              "native stack room %u)%s%s%s%s",
              ctx->app_name, path, (unsigned)size,
              mem ? (unsigned)wasm_memory_get_cur_page_count(mem) : 0u, (unsigned)max_pages,
              pool_used, stack_room,
              a.fn[FMRB_SHELL_HOOK_CREATE] ? " on_create" : "",
              a.fn[FMRB_SHELL_HOOK_UPDATE] ? " on_update" : "",
              a.fn[FMRB_SHELL_HOOK_EVENT] ? " on_event" : "",
              a.buf_fn ? " (event buffer)" : "");

    ret = fmrb_app_shell_run(ctx, &WASM_ENGINE, &a, why, sizeof(why));
    if (ret != FMRB_OK && why[0] && err && err_cap) {
        snprintf(err, err_cap, "%s", why);
    }
    if (a.inst && mem) {
        FMRB_LOGI(TAG, "[%s] ended (memory %u pages)", ctx->app_name,
                  (unsigned)wasm_memory_get_cur_page_count(mem));
    }

out:
    slot_detach(&a);
    if (a.env) wasm_runtime_destroy_exec_env(a.env);
    if (a.inst) wasm_runtime_deinstantiate(a.inst);
    if (module) wasm_runtime_unload(module);
    if (bin) fmrb_free(ctx->mem_handle, bin);
    if (a.drawer >= 0) fmrb_gfx_drawer_close(a.drawer);
    runtime_release(ctx);
    if (ret != FMRB_OK && err && err[0]) {
        FMRB_LOGE(TAG, "[%s] %s", ctx->app_name, err);
    }
    return ret;
}

/* ------------------------------------------------------------ the state */

/** The dump's tag (a literal: a static pointer would be .data in internal RAM). */
#define DTAG "fmrb_wasm"
/** How long a reader waits for s_lock: a monitor must not stall on a start. */
#define FMRB_WASM_STATS_WAIT_MS 50

void fmrb_wasm_stats(fmrb_wasm_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    out->natives = (uint32_t)NATIVES_COUNT;
    out->pool_high = s_pool_high;
    if (!s_lock) return;
    if (fmrb_semaphore_take(s_lock, FMRB_MS_TO_TICKS(FMRB_WASM_STATS_WAIT_MS)) != pdTRUE) {
        return;
    }
    out->traps = s_traps;
    out->pool_total = s_pool ? (uint32_t)FMRB_WASM_POOL_SIZE : 0u;
    out->apps = (uint32_t)s_users;
    out->running = s_users > 0;
    for (int i = 0; i < FMRB_MAX_APPS; i++) {
        if (s_holders[i] && s_slots[i].inst) out->modules++;
    }
    mem_alloc_info_t info;
    if (out->running && wasm_runtime_get_mem_alloc_info(&info)) {
        out->pool_used = (uint32_t)(info.total_size - info.total_free_size);
        if (info.highmark_size > out->pool_high) out->pool_high = (uint32_t)info.highmark_size;
    }
    fmrb_semaphore_give(s_lock);
}

bool fmrb_wasm_app_stats(int pid, fmrb_wasm_app_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!s_lock || pid < 0) return false;
    if (fmrb_semaphore_take(s_lock, FMRB_MS_TO_TICKS(FMRB_WASM_STATS_WAIT_MS)) != pdTRUE) {
        return false;
    }
    bool found = false;
    for (int i = 0; i < FMRB_MAX_APPS; i++) {
        const wasm_slot_t *sl = &s_slots[i];
        if (!s_holders[i] || !sl->inst || s_holders[i]->app_id != pid) continue;
        /* The instance stays while s_lock is held (slot_detach takes it). */
        wasm_memory_inst_t mem = wasm_runtime_get_default_memory(sl->inst);
        out->pages = mem ? (uint32_t)wasm_memory_get_cur_page_count(mem) : 0u;
        out->max_pages = sl->max_pages;
        out->binary_size = sl->binary_size;
        out->hook_us = sl->hook_us;
        out->hook_max_us = sl->hook_max_us;
        out->calls = sl->calls;
        found = true;
        break;
    }
    fmrb_semaphore_give(s_lock);
    return found;
}

void fmrb_wasm_dump_stats(void)
{
    fmrb_wasm_stats_t st;
    fmrb_wasm_stats(&st);
    FMRB_LOGI(DTAG, "runtime %s apps=%u modules=%u natives=%u pool used=%u high=%u total=%u traps=%u",
              st.running ? "on" : "off", (unsigned)st.apps, (unsigned)st.modules,
              (unsigned)st.natives, (unsigned)st.pool_used, (unsigned)st.pool_high,
              (unsigned)st.pool_total, (unsigned)st.traps);
    if (!st.modules) return;
    /* The holders' pids and names, taken under the lock and printed after
       (a line of UART is a few ms; the lock must not be held across it). */
    int pids[FMRB_MAX_APPS];
    char names[FMRB_MAX_APPS][24];
    int n = 0;
    if (fmrb_semaphore_take(s_lock, FMRB_MS_TO_TICKS(FMRB_WASM_STATS_WAIT_MS)) != pdTRUE) return;
    for (int i = 0; i < FMRB_MAX_APPS; i++) {
        if (!s_holders[i] || !s_slots[i].inst) continue;
        pids[n] = s_holders[i]->app_id;
        snprintf(names[n], sizeof(names[n]), "%s", s_holders[i]->app_name);
        n++;
    }
    fmrb_semaphore_give(s_lock);
    for (int i = 0; i < n; i++) {
        fmrb_wasm_app_stats_t as;
        if (!fmrb_wasm_app_stats(pids[i], &as)) continue;
        FMRB_LOGI(DTAG, "  pid=%d %-16s pages=%u/%u bin=%u hook=%uus max=%uus calls=%u",
                  pids[i], names[i], (unsigned)as.pages, (unsigned)as.max_pages,
                  (unsigned)as.binary_size, (unsigned)as.hook_us, (unsigned)as.hook_max_us,
                  (unsigned)as.calls);
        fmrb_task_delay_ms(1);
    }
}
