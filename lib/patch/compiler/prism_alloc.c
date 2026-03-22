/*
 * prism_alloc.c - Dedicated estalloc pool for prism parser
 *
 * Provides thread-safe memory allocation for prism's xmalloc/xfree.
 * Uses a separate estalloc pool so prism memory is isolated from VM heaps.
 *
 * Host build (picorbc): pool allocated as static array, no mutex needed.
 * Target build (ESP32/Linux): pool from g_prism_memory_pool, mutex protected.
 */

#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* estalloc header - same allocator used by VM heaps
 * Path relative to mrbgems/mruby-compiler2/lib/ after patch copy */
#include "../../picoruby-mruby/lib/estalloc/estalloc.h"

static ESTALLOC *prism_est = NULL;

#ifndef PRISM_POOL_SIZE
  #ifdef PRISM_BUILD_HOST
    #define PRISM_POOL_SIZE (288 * 1024)
  #else
    #define PRISM_POOL_SIZE (192 * 1024)
  #endif
#endif

#ifdef PRISM_BUILD_HOST
  /* Host build: local static pool, single-threaded */
  static unsigned char g_prism_memory_pool[PRISM_POOL_SIZE] __attribute__((aligned(8)));
  #define PRISM_LOCK()
  #define PRISM_UNLOCK()
#else
  /* Target build: pool from fmrb_mempool.c */
  extern unsigned char g_prism_memory_pool[];

  /*
   * Platform-specific mutex for thread safety.
   * Implemented in ports/ code (compiled by CMakeLists.txt which has
   * access to platform headers like FreeRTOS or pthread).
   */
  extern void fmrb_prism_lock(void);
  extern void fmrb_prism_unlock(void);
  #define PRISM_LOCK()   fmrb_prism_lock()
  #define PRISM_UNLOCK() fmrb_prism_unlock()
#endif

static void
prism_est_init(void)
{
  if (prism_est != NULL) return;
  prism_est = est_init(g_prism_memory_pool, PRISM_POOL_SIZE);
}

void*
fmrb_prism_malloc(size_t size)
{
  PRISM_LOCK();
  if (prism_est == NULL) prism_est_init();
  void *ptr = est_malloc(prism_est, size);
  PRISM_UNLOCK();
  return ptr;
}

void*
fmrb_prism_calloc(size_t nmemb, size_t size)
{
  size_t total = nmemb * size;
  PRISM_LOCK();
  if (prism_est == NULL) prism_est_init();
  void *ptr = est_malloc(prism_est, total);
  PRISM_UNLOCK();
  if (ptr != NULL) {
    memset(ptr, 0, total);
  }
  return ptr;
}

void*
fmrb_prism_realloc(void *ptr, size_t size)
{
  PRISM_LOCK();
  if (prism_est == NULL) prism_est_init();
  void *new_ptr = est_realloc(prism_est, ptr, size);
  PRISM_UNLOCK();
  return new_ptr;
}

void
fmrb_prism_free(void *ptr)
{
  if (ptr == NULL) return;
  PRISM_LOCK();
  if (prism_est != NULL) {
    est_free(prism_est, ptr);
  }
  PRISM_UNLOCK();
}
