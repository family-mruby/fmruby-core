/*
 * Minimal WAMR platform layer for the proof of concept (tools/wamr_poc).
 * The same file is compiled for Linux x86-64 and for emscripten.
 *
 * - Runtime allocations go to the fixed pool given to wasm_runtime_full_init
 *   (Alloc_With_Pool), so os_malloc/os_realloc should never be called. They
 *   are counted to prove it.
 * - Linear memories go through os_mmap/os_mremap. They are served from a
 *   second fixed region with WAMR's own allocator (mem_alloc), so nothing
 *   comes from the system heap after start-up.
 * - The native stack boundary is not derived here: the host sets it per
 *   exec env with wasm_runtime_set_native_stack_boundary.
 */
#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#include "mem_alloc.h"

#include <time.h>

static poc_platform_stats_t g_stats;
static mem_allocator_t g_linmem;
static unsigned g_linmem_size;

bool
poc_platform_set_linmem_region(void *buf, unsigned size)
{
    g_linmem = mem_allocator_create(buf, size);
    g_linmem_size = size;
    return g_linmem != NULL;
}

void
poc_platform_get_stats(poc_platform_stats_t *out)
{
    *out = g_stats;
}

void
poc_platform_linmem_info(unsigned *total, unsigned *free_size,
                         unsigned *highmark)
{
    /* same layout as mem_alloc_info_t in wasm_export.h */
    struct {
        uint32_t total_size, total_free_size, highmark_size;
    } info = { 0 };
    if (g_linmem)
        mem_allocator_get_alloc_info(g_linmem, &info);
    *total = info.total_size;
    *free_size = info.total_free_size;
    *highmark = info.highmark_size;
}

int
bh_platform_init(void)
{
    return 0;
}

void
bh_platform_destroy(void)
{}

/* ---- memory outside the pool (should stay unused) ---- */

void *
os_malloc(unsigned size)
{
    g_stats.os_malloc_calls++;
    return malloc(size);
}

void *
os_realloc(void *ptr, unsigned size)
{
    g_stats.os_malloc_calls++;
    return realloc(ptr, size);
}

void
os_free(void *ptr)
{
    free(ptr);
}

/* ---- output ---- */

int
os_printf(const char *format, ...)
{
    va_list ap;
    int ret;
    va_start(ap, format);
    ret = vprintf(format, ap);
    va_end(ap);
    return ret;
}

int
os_vprintf(const char *format, va_list ap)
{
    return vprintf(format, ap);
}

int
os_dumps_proc_mem_info(char *out, unsigned int size)
{
    (void)out;
    (void)size;
    return -1;
}

/* ---- time ---- */

uint64
os_time_get_boot_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64)ts.tv_sec * 1000000ULL + (uint64)ts.tv_nsec / 1000ULL;
}

uint64
os_time_thread_cputime_us(void)
{
    return os_time_get_boot_us();
}

/* ---- thread (single thread) ---- */

korp_tid
os_self_thread(void)
{
    return (korp_tid)1;
}

uint8 *
os_thread_get_stack_boundary(void)
{
    /* Set explicitly by the host (wasm_runtime_set_native_stack_boundary). */
    return NULL;
}

void
os_thread_jit_write_protect_np(bool enabled)
{
    (void)enabled;
}

int
os_mutex_init(korp_mutex *mutex)
{
    mutex->depth = 0;
    return BHT_OK;
}

int
os_mutex_destroy(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

int
os_mutex_lock(korp_mutex *mutex)
{
    mutex->depth++;
    return BHT_OK;
}

int
os_mutex_unlock(korp_mutex *mutex)
{
    mutex->depth--;
    return BHT_OK;
}

/* Condition variables: only referenced by shared/utils/runtime_timer.c,
 * which gets linked in but is not used without threads. */
int
os_cond_init(korp_cond *cond)
{
    *cond = 0;
    return BHT_OK;
}

int
os_cond_destroy(korp_cond *cond)
{
    (void)cond;
    return BHT_OK;
}

/* ---- linear memory ---- */

void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    void *p;
    (void)hint;
    (void)prot;
    (void)flags;
    (void)file;
    g_stats.os_mmap_calls++;
    if (!g_linmem || size > UINT32_MAX
        || !(p = mem_allocator_malloc(g_linmem, (uint32)size))) {
        g_stats.linmem_failures++;
        return NULL;
    }
    memset(p, 0, size);
    return p;
}

void *
os_mremap(void *old_addr, size_t old_size, size_t new_size)
{
    uint8 *p;
    g_stats.os_mremap_calls++;
    if (!g_linmem || new_size > UINT32_MAX
        || !(p = mem_allocator_realloc(g_linmem, old_addr, (uint32)new_size))) {
        g_stats.linmem_failures++;
        return NULL;
    }
    if (new_size > old_size)
        memset(p + old_size, 0, new_size - old_size);
    return p;
}

void
os_munmap(void *addr, size_t size)
{
    (void)size;
    if (g_linmem && addr)
        mem_allocator_free(g_linmem, addr);
}

int
os_mprotect(void *addr, size_t size, int prot)
{
    (void)addr;
    (void)size;
    (void)prot;
    return 0;
}

void
os_dcache_flush(void)
{}

void
os_icache_flush(void *start, size_t len)
{
    (void)start;
    (void)len;
}
