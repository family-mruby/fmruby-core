/*
 * The WAMR platform layer of Family mruby (doc/wasm_apps W1b): types and
 * small inline functions every WAMR source sees.
 *
 * - Threads are FreeRTOS tasks: each wasm app runs in its own app task with
 *   its own exec env, and the runtime's shared structures are guarded by
 *   FreeRTOS mutexes. A korp_mutex carries its own storage
 *   (xSemaphoreCreateMutexStatic), so a lock costs nothing from the FreeRTOS
 *   heap (internal RAM on the ESP32): it lives where WAMR keeps the structure
 *   that holds it -- its pool or its .bss, both in PSRAM on the P4.
 * - No WASI, no files, no hardware bound checks (software bound checks).
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
 * clash with WAMR's copy (platform_wasi_types.h). WASI is off, so use
 * emscripten's definitions and skip WAMR's header (report/w1a.md 3). */
#include <wasi/api.h>
#define _PLATFORM_WASI_TYPES_H
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BH_PLATFORM_FMRB
#define BH_PLATFORM_FMRB
#endif

typedef TaskHandle_t korp_tid;
typedef struct {
    SemaphoreHandle_t handle;
    StaticSemaphore_t storage;
} korp_mutex;
typedef int korp_cond;
typedef TaskHandle_t korp_thread;
typedef int korp_rwlock;
typedef unsigned int korp_sem;

/* No static initializer: os_mutex_init makes every lock. */
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

#ifdef __cplusplus
}
#endif

#endif /* _PLATFORM_INTERNAL_H */
