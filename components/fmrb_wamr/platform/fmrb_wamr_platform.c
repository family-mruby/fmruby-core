/*
 * The WAMR platform layer of Family mruby (doc/wasm_apps W1b), on fmrb_hal,
 * fmrb_mem and FreeRTOS. The same file for the P4, the Linux simulation and
 * the hosted builds (native, browser). It grew out of the proof of concept's
 * layer (tools/wamr_poc/host/platform, report/w1a.md 4).
 *
 * Where memory comes from (decision 5):
 * - The runtime's own allocations (module data, instances, exec envs and the
 *   operand stacks) go to the small shared pool given to
 *   wasm_runtime_full_init (Alloc_With_Pool, fmrb_wasm.c), so os_malloc is
 *   not expected to be called; it falls back to the system pool if it is.
 * - A linear memory (os_mmap / os_mremap) comes from the memory pool of the
 *   app that maps it: the app task's own frame (ctx->mem_handle), the same
 *   frame a Ruby app's VM takes. Each block remembers its pool in a small
 *   header, so it goes back to the right one whoever frees it.
 * - The native stack boundary is not derived here (NULL): fmrb_wasm.c sets
 *   it per exec env from the app task's stack.
 */
#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

#include "fmrb_app.h"
#include "fmrb_hal_time.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"

static const char *TAG = "wamr";

int
bh_platform_init(void)
{
    return 0;
}

void
bh_platform_destroy(void)
{}

/* ---- memory outside the pool (not expected to be used) ---- */

void *
os_malloc(unsigned size)
{
    return fmrb_sys_malloc(size);
}

void *
os_realloc(void *ptr, unsigned size)
{
    return fmrb_sys_realloc(ptr, size);
}

void
os_free(void *ptr)
{
    if (ptr)
        fmrb_sys_free(ptr);
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
    return (uint64)fmrb_hal_time_get_us();
}

uint64
os_time_thread_cputime_us(void)
{
    return os_time_get_boot_us();
}

/* ---- threads: FreeRTOS tasks ---- */

korp_tid
os_self_thread(void)
{
    return xTaskGetCurrentTaskHandle();
}

uint8 *
os_thread_get_stack_boundary(void)
{
    /* Set per exec env by fmrb_wasm.c (wasm_runtime_set_native_stack_boundary). */
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
    mutex->handle = xSemaphoreCreateMutexStatic(&mutex->storage);
    return mutex->handle ? BHT_OK : BHT_ERROR;
}

int
os_mutex_destroy(korp_mutex *mutex)
{
    if (mutex->handle) {
        vSemaphoreDelete(mutex->handle);
        mutex->handle = NULL;
    }
    return BHT_OK;
}

int
os_mutex_lock(korp_mutex *mutex)
{
    if (!mutex->handle)
        return BHT_ERROR;
    return xSemaphoreTake(mutex->handle, portMAX_DELAY) == pdTRUE ? BHT_OK
                                                                  : BHT_ERROR;
}

int
os_mutex_unlock(korp_mutex *mutex)
{
    if (!mutex->handle)
        return BHT_ERROR;
    return xSemaphoreGive(mutex->handle) == pdTRUE ? BHT_OK : BHT_ERROR;
}

/* Condition variables: only referenced by shared/utils/runtime_timer.c,
 * which is linked in but not used without threads. */
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

/* ---- linear memory: from the mapping app's own memory pool ---- */

/* 16 bytes keep the memory 8-byte aligned (more than WAMR asks) on every build. */
typedef union {
    fmrb_mem_handle_t pool;
    uint8_t pad[16];
} linmem_head_t;

void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    (void)hint;
    (void)prot;
    (void)flags;
    (void)file;
    fmrb_app_task_context_t *ctx = fmrb_current();
    if (!ctx || size > UINT32_MAX - sizeof(linmem_head_t)) {
        FMRB_LOGE(TAG, "linear memory of %u bytes asked outside an app",
                  (unsigned)size);
        return NULL;
    }
    linmem_head_t *h = fmrb_malloc(ctx->mem_handle, sizeof(*h) + size);
    if (!h) {
        FMRB_LOGW(TAG, "[%s] no room for a linear memory of %u bytes in the app's pool",
                  ctx->app_name, (unsigned)size);
        return NULL;
    }
    h->pool = ctx->mem_handle;
    memset(h + 1, 0, size);
    return h + 1;
}

void *
os_mremap(void *old_addr, size_t old_size, size_t new_size)
{
    if (!old_addr)
        return os_mmap(NULL, new_size, 0, 0, os_get_invalid_handle());
    linmem_head_t *h = (linmem_head_t *)old_addr - 1;
    fmrb_mem_handle_t pool = h->pool;
    if (new_size > UINT32_MAX - sizeof(linmem_head_t))
        return NULL;
    linmem_head_t *n = fmrb_realloc(pool, h, sizeof(*n) + new_size);
    if (!n)
        return NULL;
    n->pool = pool;
    if (new_size > old_size)
        memset((uint8_t *)(n + 1) + old_size, 0, new_size - old_size);
    return n + 1;
}

void
os_munmap(void *addr, size_t size)
{
    (void)size;
    if (!addr)
        return;
    linmem_head_t *h = (linmem_head_t *)addr - 1;
    fmrb_free(h->pool, h);
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
