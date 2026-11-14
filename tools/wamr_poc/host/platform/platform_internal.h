/*
 * Minimal WAMR platform layer for the proof of concept (tools/wamr_poc).
 *
 * One source for both host builds (Linux x86-64 and emscripten). It stands
 * in for the layer the firmware will write on top of fmrb_hal: no WASI, no
 * threads, no hardware bound checks (software bound checks, as on ESP32).
 */
#ifndef _PLATFORM_INTERNAL_H
#define _PLATFORM_INTERNAL_H

#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <limits.h>

#ifdef __EMSCRIPTEN__
/* emscripten's <errno.h> pulls in its own <wasi/api.h>, whose typedefs
 * clash with WAMR's copy (platform_wasi_types.h). WASI is disabled here, so
 * use emscripten's definitions and skip WAMR's header. */
#include <wasi/api.h>
#define _PLATFORM_WASI_TYPES_H
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BH_PLATFORM_FMRB_POC
#define BH_PLATFORM_FMRB_POC
#endif

/* Single-threaded PoC: the runtime only needs these to be types. The mutex
 * is a counter so that lock/unlock pairing can still be checked. */
typedef uintptr_t korp_tid;
typedef struct {
    int depth;
} korp_mutex;
typedef int korp_cond;
typedef uintptr_t korp_thread;
typedef int korp_rwlock;
typedef unsigned int korp_sem;

#define OS_THREAD_MUTEX_INITIALIZER { 0 }

#define BH_APPLET_PRESERVED_STACK_SIZE (2 * 1024)
#define BH_THREAD_DEFAULT_PRIORITY 5

static inline int
os_getpagesize(void)
{
    return 4096;
}

typedef int os_file_handle;
typedef void *os_dir_stream;
typedef int os_raw_file_handle;

static inline os_file_handle
os_get_invalid_handle(void)
{
    return -1;
}

/* Counters read by the host to prove where memory came from. */
typedef struct {
    unsigned long os_malloc_calls;   /* os_malloc/os_realloc (outside the pool) */
    unsigned long os_mmap_calls;     /* linear memory maps */
    unsigned long os_mremap_calls;   /* linear memory grows */
    unsigned long linmem_failures;   /* maps/grows refused by the region */
} poc_platform_stats_t;

/* The linear memory region (wasm memories) is a second fixed region set up
 * by the host before wasm_runtime_full_init. */
bool
poc_platform_set_linmem_region(void *buf, unsigned size);
void
poc_platform_get_stats(poc_platform_stats_t *out);
/* total/free/highmark of the linear memory region */
void
poc_platform_linmem_info(unsigned *total, unsigned *free_size,
                         unsigned *highmark);

#ifdef __cplusplus
}
#endif

#endif /* _PLATFORM_INTERNAL_H */
