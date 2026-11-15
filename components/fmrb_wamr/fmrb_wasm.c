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

/* The runtime's state (PSRAM .bss on the P4: fmrb_wamr_psram.lf). */
static fmrb_semaphore_t s_lock;
static StaticSemaphore_t s_lock_storage;
static int s_users;
static void *s_pool;
static NativeSymbol *s_natives;
/* The apps that count as users, so a forced kill can give its count back
   (fmrb_wasm_app_forget). */
static fmrb_app_task_context_t *s_holders[FMRB_MAX_APPS];

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
            if (!s_holders[i]) { s_holders[i] = ctx; break; }
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
    if (--s_users == 0) {
        mem_alloc_info_t info;
        bool have = wasm_runtime_get_mem_alloc_info(&info);
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
    if (wasm_runtime_call_wasm(a->env, f, argc, argv)) {
        return FMRB_SHELL_OK;
    }
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

    char lerr[128];
    module = wasm_runtime_load(bin, size, lerr, sizeof(lerr));
    if (!module) {
        snprintf(err, err_cap, "%s did not load: %s", path, lerr);
        goto out;
    }

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
