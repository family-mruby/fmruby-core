/*
 * WAMR proof of concept: the "firmware" side.
 *
 * One source, two builds (Linux x86-64 and emscripten/node). The host owns
 * the loop: it calls on_create once and on_update a fixed number of times,
 * then prints a checksum of the 64x64 canvas the app drew into.
 *
 * Host functions are registered in the raw form only
 * (wasm_runtime_register_natives_raw): arguments arrive as an array of
 * 64-bit slots, so the runtime never has to call a C function through a
 * signature-dependent trampoline (invokeNative), which cannot work on wasm.
 *
 * Every result line starts with "RESULT" so that run.rb can read it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "wasm_export.h"
#include "platform_internal.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/stack.h>
#define POC_BUILD "emscripten"
#else
#define POC_BUILD "linux"
#endif

#define CANVAS_W 64
#define CANVAS_H 64
#define POOL_SIZE (2 * 1024 * 1024)
#define LINMEM_SIZE (8 * 1024 * 1024)
#define WASM_STACK_SIZE (16 * 1024)
#define DEFAULT_FRAMES 100
#define CALL_INSN_LIMIT 10000000 /* per call from the host */

uint64_t os_time_get_boot_us(void);

/* Both regions are taken once at start-up (PSRAM in the firmware). */
static uint8_t *g_pool;
static uint8_t *g_linmem;
static uint8_t *g_stack_boundary;

typedef struct {
    const char *name;
    uint8_t canvas[CANVAS_W * CANVAS_H];
    unsigned log_lines;
} AppCtx;

typedef struct {
    const char *path;
    uint8_t *buf; /* copy of the .wasm in the pool; classic rewrites it */
    uint32_t size;
    wasm_module_t module;
} PocModule;

typedef struct {
    AppCtx ctx;
    wasm_module_inst_t inst;
    wasm_exec_env_t env;
} PocApp;

/* ------------------------------------------------------------------ */
/* host functions (raw form)                                           */
/* ------------------------------------------------------------------ */

static AppCtx *
ctx_of(wasm_exec_env_t env)
{
    return (AppCtx *)wasm_runtime_get_custom_data(
        wasm_runtime_get_module_inst(env));
}

/* fmrb_log(ptr, len): signature "(*~)" lets the runtime bound-check the
 * buffer and hand over a native pointer. */
static void
n_log(wasm_exec_env_t env, uint64_t *args)
{
    const char *p = *(const char **)&args[0];
    uint32_t len = *(uint32_t *)&args[1];
    AppCtx *ctx = ctx_of(env);
    ctx->log_lines++;
    printf("LOG [%s] %.*s\n", ctx->name, (int)len, p);
}

/* fmrb_plot(x, y, color) */
static void
n_plot(wasm_exec_env_t env, uint64_t *args)
{
    int32_t x = *(int32_t *)&args[0];
    int32_t y = *(int32_t *)&args[1];
    int32_t c = *(int32_t *)&args[2];
    AppCtx *ctx = ctx_of(env);
    if (x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H)
        ctx->canvas[y * CANVAS_W + x] = (uint8_t)c;
}

/* fmrb_millis() -> i64 */
static void
n_millis(wasm_exec_env_t env, uint64_t *args)
{
    (void)env;
    *(int64_t *)&args[0] = (int64_t)(os_time_get_boot_us() / 1000);
}

/* fmrb_nop(v) -> i32 (benchmark of the call path only) */
static void
n_nop(wasm_exec_env_t env, uint64_t *args)
{
    (void)env;
    int32_t v = *(int32_t *)&args[0];
    *(int32_t *)&args[0] = v & 1;
}

static NativeSymbol g_natives[] = {
    { "fmrb_log", (void *)n_log, "(*~)", NULL },
    { "fmrb_plot", (void *)n_plot, "(iii)", NULL },
    { "fmrb_millis", (void *)n_millis, "()I", NULL },
    { "fmrb_nop", (void *)n_nop, "(i)i", NULL },
};

#ifdef POC_BOUNDARY
/* ------------------------------------------------------------------ */
/* the app boundary (doc/app_boundary W4a)                             */
/* ------------------------------------------------------------------ */

/* The generated raw natives call the flat functions without the exec env,
 * as the firmware's do; the host is single-threaded, so the app being
 * called is kept here. */
static AppCtx *g_current;

/* The firmware's body of the flat function, played on the 64x64 canvas:
 * receiver 0 is that canvas. */
int32_t
fmrb_gfx_fill_rect(int32_t gfx, int32_t x, int32_t y, int32_t w, int32_t h,
                   int32_t color)
{
    if (gfx != 0 || !g_current)
        return -1;
    for (int32_t yy = y; yy < y + h; yy++)
        for (int32_t xx = x; xx < x + w; xx++)
            if (xx >= 0 && xx < CANVAS_W && yy >= 0 && yy < CANVAS_H)
                g_current->canvas[yy * CANVAS_W + xx] = (uint8_t)color;
    return 0;
}

#include "fmrb_gen/fmrb_boundary_wasm.inc"

static NativeSymbol g_boundary_natives[] = { FMRB_GEN_WASM_NATIVES };

/* Every other flat function: a stand-in that hashes its arguments as they
 * arrived (FNV-1a over the name and the bytes of each), so the ABI tour of
 * the C app (apps/c_boundary/tour.h) can be compared with the same calls
 * made natively. */
static uint32_t g_stub_hash = 2166136261u;
static int g_stub_calls;

static void
stub_mix(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        g_stub_hash ^= p[i];
        g_stub_hash *= 16777619u;
    }
}

void
fmrb_stub_begin(const char *name)
{
    g_stub_calls++;
    stub_mix(name, strlen(name) + 1);
}

void
fmrb_stub_arg(const void *data, size_t len)
{
    stub_mix(&len, sizeof(uint32_t));
    stub_mix(data, len);
}

#define FMRB_STUB_HAVE_fmrb_gfx_fill_rect
#include "fmrb_gen/fmrb_boundary_stub.inc"
#include "../apps/c_boundary/tour.h"
#endif

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static uint32_t
fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/* The pattern every app draws, computed directly in C. */
static uint32_t
expected_checksum(int frames)
{
    static uint8_t canvas[CANVAS_W * CANVAS_H];
    memset(canvas, 0, sizeof(canvas));
    for (int f = 0; f < frames; f++)
        for (int i = 0; i < 64; i++)
            canvas[((i * 13 + f * 5) & 63) * CANVAS_W + ((i * 7 + f * 3) & 63)] =
                (uint8_t)((i * 31 + f * 17) & 255);
    return fnv1a(canvas, sizeof(canvas));
}

static void
print_pool(const char *label)
{
    mem_alloc_info_t info;
    unsigned lt, lf, lh;
    poc_platform_stats_t st;
    wasm_runtime_get_mem_alloc_info(&info);
    poc_platform_linmem_info(&lt, &lf, &lh);
    poc_platform_get_stats(&st);
    printf("RESULT pool label=%s total=%u free=%u highmark=%u "
           "linmem_total=%u linmem_free=%u linmem_highmark=%u "
           "os_malloc_calls=%lu os_mmap_calls=%lu os_mremap_calls=%lu "
           "linmem_failures=%lu\n",
           label, info.total_size, info.total_free_size, info.highmark_size,
           lt, lf, lh, st.os_malloc_calls, st.os_mmap_calls,
           st.os_mremap_calls, st.linmem_failures);
}

static bool
runtime_init(void)
{
    RuntimeInitArgs init;
    g_pool = malloc(POOL_SIZE);
    g_linmem = malloc(LINMEM_SIZE);
    if (!g_pool || !g_linmem)
        return false;
    if (!poc_platform_set_linmem_region(g_linmem, LINMEM_SIZE))
        return false;
    memset(&init, 0, sizeof(init));
    init.mem_alloc_type = Alloc_With_Pool;
    init.mem_alloc_option.pool.heap_buf = g_pool;
    init.mem_alloc_option.pool.heap_size = POOL_SIZE;
    if (!wasm_runtime_full_init(&init)) {
        printf("RESULT error stage=init\n");
        return false;
    }
    if (!wasm_runtime_register_natives_raw(
            "env", g_natives, sizeof(g_natives) / sizeof(g_natives[0]))) {
        printf("RESULT error stage=register_natives_raw\n");
        return false;
    }
#ifdef POC_BOUNDARY
    if (!wasm_runtime_register_natives_raw(
            "fmrb", g_boundary_natives,
            sizeof(g_boundary_natives) / sizeof(g_boundary_natives[0]))) {
        printf("RESULT error stage=register_boundary\n");
        return false;
    }
#endif
    return true;
}

static bool
module_load(PocModule *m, const char *path)
{
    char err[256];
    FILE *fp = fopen(path, "rb");
    long n;
    memset(m, 0, sizeof(*m));
    m->path = path;
    if (!fp) {
        printf("RESULT error stage=open path=%s\n", path);
        return false;
    }
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    /* The copy of the binary lives in the pool too. */
    m->buf = wasm_runtime_malloc((uint32_t)n);
    if (!m->buf || fread(m->buf, 1, (size_t)n, fp) != (size_t)n) {
        fclose(fp);
        printf("RESULT error stage=read path=%s\n", path);
        return false;
    }
    fclose(fp);
    m->size = (uint32_t)n;
    m->module = wasm_runtime_load(m->buf, m->size, err, sizeof(err));
    if (!m->module) {
        printf("RESULT error stage=load path=%s msg=\"%s\"\n", path, err);
        return false;
    }
    return true;
}

static void
module_unload(PocModule *m)
{
    if (m->module)
        wasm_runtime_unload(m->module);
    if (m->buf)
        wasm_runtime_free(m->buf);
    m->module = NULL;
    m->buf = NULL;
}

static uint32_t
mem_pages(PocApp *a)
{
    wasm_memory_inst_t mem = wasm_runtime_get_default_memory(a->inst);
    return mem ? (uint32_t)wasm_memory_get_cur_page_count(mem) : 0;
}

/* Calls an export. Returns false (and prints why) on an exception. The
 * result, if any, is stored in *ret. */
static bool
call(PocApp *a, const char *fn, int insn_limit, int nargs, uint32_t a0,
     uint32_t a1, int32_t *ret)
{
    wasm_function_inst_t f = wasm_runtime_lookup_function(a->inst, fn);
    uint32_t argv[4] = { a0, a1, 0, 0 };
    if (!f) {
        printf("RESULT error stage=lookup app=%s fn=%s\n", a->ctx.name, fn);
        return false;
    }
    wasm_runtime_set_instruction_count_limit(a->env, insn_limit);
#ifdef POC_BOUNDARY
    g_current = &a->ctx;
#endif
    if (!wasm_runtime_call_wasm(a->env, f, (uint32_t)nargs, argv)) {
        return false;
    }
    if (ret)
        *ret = (int32_t)argv[0];
    return true;
}

static const char *
exception_of(PocApp *a)
{
    const char *e = wasm_runtime_get_exception(a->inst);
    return e ? e : "(none)";
}

static bool
app_start(PocApp *a, PocModule *m, const char *name, uint32_t max_pages)
{
    char err[256];
    struct InstantiationArgs2 *args;
    memset(a, 0, sizeof(*a));
    a->ctx.name = name;
    if (!wasm_runtime_instantiation_args_create(&args))
        return false;
    wasm_runtime_instantiation_args_set_default_stack_size(args,
                                                           WASM_STACK_SIZE);
    wasm_runtime_instantiation_args_set_host_managed_heap_size(args, 0);
    if (max_pages)
        wasm_runtime_instantiation_args_set_max_memory_pages(args, max_pages);
    a->inst = wasm_runtime_instantiate_ex2(m->module, args, err, sizeof(err));
    wasm_runtime_instantiation_args_destroy(args);
    if (!a->inst) {
        printf("RESULT error stage=instantiate app=%s msg=\"%s\"\n", name,
               err);
        return false;
    }
    wasm_runtime_set_custom_data(a->inst, &a->ctx);
    a->env = wasm_runtime_create_exec_env(a->inst, WASM_STACK_SIZE);
    if (!a->env) {
        printf("RESULT error stage=exec_env app=%s\n", name);
        return false;
    }
    wasm_runtime_set_native_stack_boundary(a->env, g_stack_boundary);
    /* TinyGo (and WASI reactors) need _initialize once before anything. */
    if (wasm_runtime_lookup_function(a->inst, "_initialize")) {
        if (!call(a, "_initialize", CALL_INSN_LIMIT, 0, 0, 0, NULL)) {
            printf("RESULT error stage=_initialize app=%s exc=\"%s\"\n", name,
                   exception_of(a));
            return false;
        }
    }
    if (!call(a, "on_create", CALL_INSN_LIMIT, 0, 0, 0, NULL)) {
        printf("RESULT error stage=on_create app=%s exc=\"%s\"\n", name,
               exception_of(a));
        return false;
    }
    return true;
}

static void
app_stop(PocApp *a)
{
    if (a->env)
        wasm_runtime_destroy_exec_env(a->env);
    if (a->inst)
        wasm_runtime_deinstantiate(a->inst);
    a->env = NULL;
    a->inst = NULL;
}

static bool
app_update(PocApp *a, int32_t *next_ms)
{
    if (!call(a, "on_update", CALL_INSN_LIMIT, 0, 0, 0, next_ms)) {
        printf("RESULT error stage=on_update app=%s exc=\"%s\"\n",
               a->ctx.name, exception_of(a));
        return false;
    }
    return true;
}

static const char *
label_of(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/* ------------------------------------------------------------------ */
/* commands                                                            */
/* ------------------------------------------------------------------ */

/* A1/A2/A3/A5: run one app for DEFAULT_FRAMES frames. */
static int
cmd_run(const char *path)
{
    PocModule m;
    PocApp a;
    int32_t next_ms = -1;
    print_pool("start");
    if (!module_load(&m, path))
        return 1;
    print_pool("loaded");
    if (!app_start(&a, &m, label_of(path), 0))
        return 1;
    for (int i = 0; i < DEFAULT_FRAMES; i++)
        if (!app_update(&a, &next_ms))
            return 1;
    /* call_indirect through the app's function table: expect 1,2,87 */
    int32_t d[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; k++)
        if (!call(&a, "dispatch", CALL_INSN_LIMIT, 1, (uint32_t)k, 0, &d[k])) {
            printf("RESULT error stage=dispatch app=%s exc=\"%s\"\n",
                   a.ctx.name, exception_of(&a));
            return 1;
        }
    printf("RESULT run app=%s build=%s frames=%d next_ms=%d checksum=%08x "
           "expected=%08x dispatch=%d,%d,%d wasm_size=%u mem_pages=%u "
           "log_lines=%u\n",
           a.ctx.name, POC_BUILD, DEFAULT_FRAMES, next_ms,
           fnv1a(a.ctx.canvas, sizeof(a.ctx.canvas)),
           expected_checksum(DEFAULT_FRAMES), d[0], d[1], d[2], m.size,
           mem_pages(&a), a.ctx.log_lines);
    print_pool("running");
    app_stop(&a);
    module_unload(&m);
    print_pool("unloaded");
    return 0;
}

#ifdef POC_BOUNDARY
/* The pattern the C boundary app draws, computed directly in C. */
static uint32_t
expected_boundary_checksum(int frames)
{
    static uint8_t canvas[CANVAS_W * CANVAS_H];
    memset(canvas, 0, sizeof(canvas));
    for (int f = 0; f < frames; f++)
        for (int i = 0; i < 8; i++) {
            int x = (i * 7 + f * 3) & 63, y = (i * 13 + f * 5) & 63;
            int w = 1 + (i & 3), h = 1 + ((i >> 1) & 3);
            for (int yy = y; yy < y + h && yy < CANVAS_H; yy++)
                for (int xx = x; xx < x + w && xx < CANVAS_W; xx++)
                    canvas[yy * CANVAS_W + xx] = (uint8_t)((i * 31 + f * 17) & 255);
        }
    return fnv1a(canvas, sizeof(canvas));
}

/* W4a: a C app calls the generated boundary function. */
static int
cmd_boundary(const char *path)
{
    PocModule m;
    PocApp a;
    int32_t next_ms = -1;
    /* the tour made natively first: what the stand-ins should see */
    g_stub_hash = 2166136261u;
    g_stub_calls = 0;
    boundary_tour();
    uint32_t abi_expected = g_stub_hash;
    int calls_expected = g_stub_calls;
    g_stub_hash = 2166136261u;
    g_stub_calls = 0;
    if (!module_load(&m, path) || !app_start(&a, &m, label_of(path), 0))
        return 1;
    uint32_t abi = g_stub_hash;
    int calls = g_stub_calls;
    for (int i = 0; i < DEFAULT_FRAMES; i++)
        if (!app_update(&a, &next_ms))
            return 1;
    printf("RESULT boundary app=%s build=%s frames=%d next_ms=%d "
           "checksum=%08x expected=%08x abi=%08x/%d abi_expected=%08x/%d wasm_size=%u\n",
           a.ctx.name, POC_BUILD, DEFAULT_FRAMES, next_ms,
           fnv1a(a.ctx.canvas, sizeof(a.ctx.canvas)),
           expected_boundary_checksum(DEFAULT_FRAMES), abi, calls, abi_expected,
           calls_expected, m.size);
    app_stop(&a);
    module_unload(&m);
    return 0;
}
#endif

static bool
read_stack_pointer(PocApp *a, uint32_t *sp)
{
    wasm_global_inst_t g;
    if (!wasm_runtime_get_export_global_inst(a->inst, "__stack_pointer", &g))
        return false;
    *sp = *(uint32_t *)g.global_data;
    return true;
}

/* A4: a call that never returns is stopped by the instruction limit; then
 * check whether the same instance can go on. */
static int
cmd_spin(const char *path)
{
    PocModule m;
    PocApp a;
    int32_t next_ms;
    uint32_t sp0 = 0, sp1 = 0;
    bool has_sp;
    uint64_t t0, t1;
    if (!module_load(&m, path) || !app_start(&a, &m, label_of(path), 0))
        return 1;
    for (int i = 0; i < DEFAULT_FRAMES / 2; i++)
        if (!app_update(&a, &next_ms))
            return 1;
    has_sp = read_stack_pointer(&a, &sp0);
    t0 = os_time_get_boot_us();
    bool ok = call(&a, "spin", CALL_INSN_LIMIT, 0, 0, 0, NULL);
    t1 = os_time_get_boot_us();
    printf("RESULT spin app=%s build=%s returned=%s exc=\"%s\" limit=%d "
           "elapsed_us=%llu\n",
           a.ctx.name, POC_BUILD, ok ? "yes" : "no", exception_of(&a),
           CALL_INSN_LIMIT, (unsigned long long)(t1 - t0));
    if (has_sp)
        read_stack_pointer(&a, &sp1);
    wasm_runtime_clear_exception(a.inst);
    /* resume: same instance, the remaining frames */
    bool resumed = true;
    for (int i = 0; i < DEFAULT_FRAMES / 2; i++)
        if (!app_update(&a, &next_ms)) {
            resumed = false;
            break;
        }
    printf("RESULT spin_resume app=%s build=%s resumed=%s checksum=%08x "
           "expected=%08x stack_pointer=%s before=%u after=%u\n",
           a.ctx.name, POC_BUILD, resumed ? "yes" : "no",
           fnv1a(a.ctx.canvas, sizeof(a.ctx.canvas)),
           expected_checksum(DEFAULT_FRAMES), has_sp ? "exported" : "hidden",
           sp0, sp1);
    /* a second spin on the same instance */
    ok = call(&a, "spin", CALL_INSN_LIMIT, 0, 0, 0, NULL);
    printf("RESULT spin_again app=%s build=%s returned=%s exc=\"%s\"\n",
           a.ctx.name, POC_BUILD, ok ? "yes" : "no", exception_of(&a));
    app_stop(&a);
    module_unload(&m);
    return 0;
}

/* A6: two modules, and two instances of one module, updated alternately. */
static int
cmd_multi(const char *path_a, const char *path_b)
{
    PocModule ma, mb;
    PocApp a, b, a2;
    int32_t ms;
    uint32_t expect = expected_checksum(DEFAULT_FRAMES);
    if (!module_load(&ma, path_a) || !module_load(&mb, path_b))
        return 1;
    /* two modules */
    if (!app_start(&a, &ma, label_of(path_a), 0)
        || !app_start(&b, &mb, label_of(path_b), 0))
        return 1;
    for (int i = 0; i < DEFAULT_FRAMES; i++)
        if (!app_update(&a, &ms) || !app_update(&b, &ms))
            return 1;
    printf("RESULT multi_modules build=%s a=%s a_checksum=%08x b=%s "
           "b_checksum=%08x expected=%08x\n",
           POC_BUILD, a.ctx.name, fnv1a(a.ctx.canvas, sizeof(a.ctx.canvas)),
           b.ctx.name, fnv1a(b.ctx.canvas, sizeof(b.ctx.canvas)), expect);
    print_pool("two_modules");
    app_stop(&b);
    /* two instances of module A; the second starts 30 frames later so the
     * frame counters differ while they are interleaved */
    app_stop(&a);
    if (!app_start(&a, &ma, "inst1", 0))
        return 1;
    for (int i = 0; i < 30; i++)
        if (!app_update(&a, &ms))
            return 1;
    if (!app_start(&a2, &ma, "inst2", 0))
        return 1;
    for (int i = 30; i < DEFAULT_FRAMES; i++)
        if (!app_update(&a, &ms) || !app_update(&a2, &ms))
            return 1;
    for (int i = 0; i < 30; i++)
        if (!app_update(&a2, &ms))
            return 1;
    printf("RESULT multi_instances build=%s module=%s inst1_checksum=%08x "
           "inst2_checksum=%08x expected=%08x\n",
           POC_BUILD, label_of(path_a),
           fnv1a(a.ctx.canvas, sizeof(a.ctx.canvas)),
           fnv1a(a2.ctx.canvas, sizeof(a2.ctx.canvas)), expect);
    print_pool("two_instances");
    app_stop(&a);
    app_stop(&a2);
    module_unload(&ma);
    module_unload(&mb);
    return 0;
}

/* A7: app A gets a small max page count and tries to grow past it; app B
 * keeps running. */
static int
cmd_limit(const char *path_a, const char *path_b, uint32_t extra_pages)
{
    PocModule ma, mb;
    PocApp probe, a, b;
    int32_t ms, r = 0;
    uint32_t init_pages, max_pages;
    if (!module_load(&ma, path_a) || !module_load(&mb, path_b))
        return 1;
    /* learn the initial size, then cap A at initial + extra_pages */
    if (!app_start(&probe, &ma, "probe", 0))
        return 1;
    init_pages = mem_pages(&probe);
    app_stop(&probe);
    max_pages = init_pages + extra_pages;
    if (!app_start(&a, &ma, label_of(path_a), max_pages)
        || !app_start(&b, &mb, label_of(path_b), 0))
        return 1;
    for (int i = 0; i < DEFAULT_FRAMES / 2; i++)
        if (!app_update(&a, &ms) || !app_update(&b, &ms))
            return 1;
    /* within the cap */
    bool ok1 = call(&a, "grow", CALL_INSN_LIMIT, 1, 1, 0, &r);
    printf("RESULT limit_grow_small app=%s build=%s init_pages=%u max_pages=%u "
           "ok=%s ret=%d pages=%u exc=\"%s\"\n",
           a.ctx.name, POC_BUILD, init_pages, max_pages, ok1 ? "yes" : "no",
           r, mem_pages(&a), ok1 ? "(none)" : exception_of(&a));
    wasm_runtime_clear_exception(a.inst);
    /* past the cap */
    r = 0;
    bool ok2 = call(&a, "grow", CALL_INSN_LIMIT, 1, extra_pages + 4, 0, &r);
    bool refused = !ok2 || r < 0;
    printf("RESULT limit_grow_big app=%s build=%s request=%u ok=%s ret=%d "
           "pages=%u refused=%s exc=\"%s\"\n",
           a.ctx.name, POC_BUILD, extra_pages + 4, ok2 ? "yes" : "no", r,
           mem_pages(&a), refused ? "yes" : "no",
           ok2 ? "(none)" : exception_of(&a));
    wasm_runtime_clear_exception(a.inst);
    /* B keeps going */
    bool b_ok = true;
    for (int i = DEFAULT_FRAMES / 2; i < DEFAULT_FRAMES; i++)
        if (!app_update(&b, &ms)) {
            b_ok = false;
            break;
        }
    /* and A itself, after the refusal? */
    bool a_ok = true;
    for (int i = DEFAULT_FRAMES / 2; i < DEFAULT_FRAMES; i++)
        if (!app_update(&a, &ms)) {
            a_ok = false;
            break;
        }
    printf("RESULT limit_other app=%s build=%s ok=%s checksum=%08x "
           "expected=%08x a_continues=%s a_checksum=%08x\n",
           b.ctx.name, POC_BUILD, b_ok ? "yes" : "no",
           fnv1a(b.ctx.canvas, sizeof(b.ctx.canvas)),
           expected_checksum(DEFAULT_FRAMES), a_ok ? "yes" : "no",
           fnv1a(a.ctx.canvas, sizeof(a.ctx.canvas)));
    print_pool("limit");
    app_stop(&a);
    app_stop(&b);
    module_unload(&ma);
    module_unload(&mb);
    return 0;
}

/* Same integer loop as the apps' bench_compute. */
static int32_t __attribute__((noinline))
native_compute(int32_t n)
{
    uint32_t x = 1, acc = 0;
    for (int32_t i = 0; i < n; i++) {
        x = x * 1103515245u + 12345u;
        acc += (x >> 16) ^ (uint32_t)i;
    }
    return (int32_t)acc;
}

static int
cmd_bench(const char *path, int32_t n_compute, int32_t n_calls)
{
    PocModule m;
    PocApp a;
    int32_t r = 0;
    uint64_t t0, t1, t_wasm, t_c, t_noop, t_host;
    volatile int32_t vn = n_compute;
    if (!module_load(&m, path) || !app_start(&a, &m, label_of(path), 0))
        return 1;

    t0 = os_time_get_boot_us();
    if (!call(&a, "bench_compute", INT_MAX, 1, (uint32_t)n_compute, 0, &r)) {
        printf("RESULT error stage=bench_compute exc=\"%s\"\n",
               exception_of(&a));
        return 1;
    }
    t1 = os_time_get_boot_us();
    t_wasm = t1 - t0;
    int32_t r_wasm = r;

    t0 = os_time_get_boot_us();
    r = native_compute(vn);
    t1 = os_time_get_boot_us();
    t_c = t1 - t0;

    wasm_function_inst_t f = wasm_runtime_lookup_function(a.inst, "noop");
    t0 = os_time_get_boot_us();
    for (int32_t i = 0; i < n_calls; i++) {
        wasm_runtime_set_instruction_count_limit(a.env, CALL_INSN_LIMIT);
        if (!wasm_runtime_call_wasm(a.env, f, 0, NULL))
            return 1;
    }
    t1 = os_time_get_boot_us();
    t_noop = t1 - t0;

    t0 = os_time_get_boot_us();
    if (!call(&a, "bench_host_calls", INT_MAX, 1, (uint32_t)n_calls, 0, NULL))
        return 1;
    t1 = os_time_get_boot_us();
    t_host = t1 - t0;

    printf("RESULT bench app=%s build=%s compute_n=%d compute_us=%llu "
           "compute_result=%d native_c_us=%llu native_c_result=%d "
           "calls_n=%d host_to_app_us=%llu app_to_host_us=%llu\n",
           a.ctx.name, POC_BUILD, n_compute, (unsigned long long)t_wasm,
           r_wasm, (unsigned long long)t_c, r, n_calls,
           (unsigned long long)t_noop, (unsigned long long)t_host);
    app_stop(&a);
    module_unload(&m);
    return 0;
}

/* TinyGo GC check: allocate and drop n buffers, report the linear memory. */
static int
cmd_churn(const char *path, int32_t n, int32_t size)
{
    PocModule m;
    PocApp a;
    if (!module_load(&m, path) || !app_start(&a, &m, label_of(path), 0))
        return 1;
    uint32_t before = mem_pages(&a);
    bool ok = call(&a, "alloc_churn", INT_MAX, 2, (uint32_t)n, (uint32_t)size,
                   NULL);
    printf("RESULT churn app=%s build=%s n=%d size=%d ok=%s pages_before=%u "
           "pages_after=%u exc=\"%s\"\n",
           a.ctx.name, POC_BUILD, n, size, ok ? "yes" : "no", before,
           mem_pages(&a), ok ? "(none)" : exception_of(&a));
    app_stop(&a);
    module_unload(&m);
    return 0;
}

int
main(int argc, char **argv)
{
    uint8_t here;
#ifdef __EMSCRIPTEN__
    /* the C stack of an emscripten program lives in its linear memory */
    g_stack_boundary = (uint8_t *)emscripten_stack_get_end();
#else
    /* main thread: 8 MiB by default; leave the PoC 1 MiB of it */
    g_stack_boundary = (uint8_t *)((uintptr_t)&here - 1024 * 1024);
#endif
    (void)here;
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s run|spin|bench|churn <app.wasm> [args]\n"
                "       %s multi|limit <a.wasm> <b.wasm>\n",
                argv[0], argv[0]);
        return 2;
    }
    if (!runtime_init())
        return 1;
    const char *cmd = argv[1];
    int rc;
    if (!strcmp(cmd, "run"))
        rc = cmd_run(argv[2]);
#ifdef POC_BOUNDARY
    else if (!strcmp(cmd, "boundary"))
        rc = cmd_boundary(argv[2]);
#endif
    else if (!strcmp(cmd, "spin"))
        rc = cmd_spin(argv[2]);
    else if (!strcmp(cmd, "multi") && argc >= 4)
        rc = cmd_multi(argv[2], argv[3]);
    else if (!strcmp(cmd, "limit") && argc >= 4)
        rc = cmd_limit(argv[2], argv[3], 2);
    else if (!strcmp(cmd, "bench"))
        rc = cmd_bench(argv[2], argc > 3 ? atoi(argv[3]) : 1000000,
                       argc > 4 ? atoi(argv[4]) : 100000);
    else if (!strcmp(cmd, "churn"))
        rc = cmd_churn(argv[2], argc > 3 ? atoi(argv[3]) : 1000,
                       argc > 4 ? atoi(argv[4]) : 4096);
    else {
        fprintf(stderr, "unknown command %s\n", cmd);
        rc = 2;
    }
    wasm_runtime_destroy();
    printf("RESULT done build=%s rc=%d\n", POC_BUILD, rc);
    return rc;
}
