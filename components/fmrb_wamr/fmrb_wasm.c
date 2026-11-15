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
 * The app runs on the app shell (fmrb_app_shell.h), as a Lua app does:
 *
 *   _initialize()                  run once first, when exported (TinyGo,
 *                                  WASI reactors): the shell's LOAD
 *   on_create()
 *   on_update() -> i32             ms until the next call (negative: the
 *                                  default FMRB_SHELL_DEFAULT_UPDATE_MS)
 *   on_event(kind, x, y, button, keycode, modifier, character, value)
 *   on_destroy(), on_suspend(), on_resume(), on_quit_request()
 *   on_resize(w, h)
 *
 * The hook signatures are this phase's own (W1b); doc/app_boundary W4b-3
 * puts the hooks in sig/ and the generated headers, and this table follows
 * it then. on_event's arguments by kind (fmrb_hid_event_type_t):
 *   key_down / key_up:      keycode, modifier, character
 *   mouse_down / mouse_up:  x, y, button (x, y in the app's area)
 *   mouse_move:             x, y
 *   mouse_wheel:            x, y, value = delta
 *   gamepad_down / _up:     button, value = gamepad id
 *   gamepad_axis:           button = axis, value = value, keycode = gamepad id
 *   kana_mode:              value = mode
 *
 * The window's FmrbGfx is drawer 0 (the shell's layout opens it before
 * on_create; no other drawer is open then): every fmrb_gfx_* call of the app
 * takes 0 as its receiver.
 *
 * A call that traps, or runs past FMRB_WASM_CALL_INSN_LIMIT instructions,
 * ends the app with the reason (decision 3): it is not called again.
 */
#include "fmrb_wasm.h"

#include <stdio.h>
#include <string.h>

#include "wasm_export.h"

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
#elif !defined(__EMSCRIPTEN__)
#include <pthread.h>
#endif

/* The generated raw natives of the boundary (tools/boundary/gen.rb). */
#include "fmrb_gen/fmrb_boundary_wasm.inc"

static const char *TAG = "wasm";

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

static bool runtime_acquire(char *err, size_t err_cap)
{
    if (!s_lock) {
        snprintf(err, err_cap, "wasm: the runtime was not set up at boot");
        return false;
    }
    fmrb_semaphore_take(s_lock, portMAX_DELAY);
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
                FMRB_LOGI(TAG, "runtime started (pool %u KB at %p, %u boundary functions)",
                          (unsigned)(FMRB_WASM_POOL_SIZE / 1024), s_pool,
                          (unsigned)NATIVES_COUNT);
            }
        }
    }
    if (ok) {
        s_users++;
    }
    fmrb_semaphore_give(s_lock);
    return ok;
}

static void runtime_release(void)
{
    fmrb_semaphore_take(s_lock, portMAX_DELAY);
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
    /* The browser build: the check is off for now (doc/wasm_apps W1b). */
    return NULL;
#endif
}

/* ------------------------------------------------------------ the app */

typedef struct {
    fmrb_app_task_context_t *ctx;
    wasm_module_inst_t inst;
    wasm_exec_env_t env;
    wasm_function_inst_t fn[FMRB_SHELL_HOOK_COUNT];
    int32_t drawer;
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
    [FMRB_SHELL_HOOK_EVENT] = 8, [FMRB_SHELL_HOOK_RESIZE] = 2,
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
    int32_t x = 0, y = 0, button = 0, keycode = 0, modifier = 0, character = 0, value = 0;
    switch (ev->kind) {
    case FMRB_HID_EVENT_KEY_DOWN:
    case FMRB_HID_EVENT_KEY_UP:
        keycode = ev->keycode; modifier = ev->modifier; character = ev->character;
        break;
    case FMRB_HID_EVENT_MOUSE_DOWN:
    case FMRB_HID_EVENT_MOUSE_UP:
        button = ev->button;
        /* fall through */
    case FMRB_HID_EVENT_MOUSE_MOVE:
        x = ev->x; y = ev->y;
        break;
    case FMRB_HID_EVENT_MOUSE_WHEEL:
        x = ev->x; y = ev->y; value = ev->delta;
        break;
    case FMRB_HID_EVENT_GAMEPAD_DOWN:
    case FMRB_HID_EVENT_GAMEPAD_UP:
        button = ev->button; value = ev->gamepad_id;
        break;
    case FMRB_HID_EVENT_GAMEPAD_AXIS:
        button = ev->axis; value = ev->value; keycode = ev->gamepad_id;
        break;
    case FMRB_HID_EVENT_KANA_MODE:
        value = ev->mode;
        break;
    default:
        return FMRB_SHELL_OK;
    }
    uint32_t argv[8] = {
        (uint32_t)ev->kind, (uint32_t)x, (uint32_t)y, (uint32_t)button,
        (uint32_t)keycode, (uint32_t)modifier, (uint32_t)character, (uint32_t)value,
    };
    return call_hook(a, FMRB_SHELL_HOOK_EVENT, 8, argv);
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
    return true;
}

fmrb_err_t fmrb_wasm_app_run(fmrb_app_task_context_t *ctx, const char *path,
                             char *err, size_t err_cap)
{
    char why[160];
    if (err && err_cap) err[0] = '\0';
    if (!runtime_acquire(err, err_cap)) {
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
    wasm_runtime_set_native_stack_boundary(a.env, native_stack_boundary());
    if (!find_hooks(&a, err, err_cap)) goto out;

    wasm_memory_inst_t mem = wasm_runtime_get_default_memory(a.inst);
    FMRB_LOGI(TAG, "[%s] loaded %s (%u bytes, memory %u pages, up to %u)%s%s%s",
              ctx->app_name, path, (unsigned)size,
              mem ? (unsigned)wasm_memory_get_cur_page_count(mem) : 0u, (unsigned)max_pages,
              a.fn[FMRB_SHELL_HOOK_CREATE] ? " on_create" : "",
              a.fn[FMRB_SHELL_HOOK_UPDATE] ? " on_update" : "",
              a.fn[FMRB_SHELL_HOOK_EVENT] ? " on_event" : "");

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
    runtime_release();
    if (ret != FMRB_OK && err && err[0]) {
        FMRB_LOGE(TAG, "[%s] %s", ctx->app_name, err);
    }
    return ret;
}
