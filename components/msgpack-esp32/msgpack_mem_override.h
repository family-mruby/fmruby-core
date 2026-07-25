/* msgpack_mem_override.h -- route msgpack-c's allocations to the fmrb SYSTEM pool.
 *
 * Force-injected into every msgpack-c translation unit via
 * `-include msgpack_mem_override.h` (PRIVATE, see this component's
 * CMakeLists.txt). msgpack-c is vendored third-party C that calls libc
 * malloc/realloc/free directly (zone.c, unpack.c, vrefbuffer.c) and exposes no
 * allocator hook, so it was the one component still bypassing fmrb_mem.h.
 *
 * On ESP32 plain malloc only ever returns internal DRAM -- CONFIG_SPIRAM_USE_MALLOC
 * is off, PSRAM is reachable only through the pools. msgpack_unpack_next()
 * allocates a fresh zone per received message, so every link frame was churning
 * internal DRAM, and it is the first thing to fail when that heap runs low
 * (MSGPACK_UNPACK_NOMEM_ERROR). Routing it to the SYSTEM pool puts the churn in
 * PSRAM where there are megabytes to spare.
 *
 * Rather than patch ~20 call sites in vendored sources (hostile to upstream
 * merges, and lib/ patching is the project's rule for submodules), remap the
 * libc names with function-like macros -- the same shape as
 * components/fmrb_spinel_rt/spinel_rt/sp_mem_override.h. stdlib.h/string.h are
 * pulled in FIRST so the real declarations are seen before the names are
 * shadowed.
 *
 * Safety note: only the call form `name(` is remapped, so a bare function-pointer
 * reference to `free` would stay libc. None exist in the compiled sources.
 * Nothing may be allocated here before fmrb_mem_init() runs -- fmrb_sys_malloc
 * returns NULL and logs if the pool handle is not up yet, so such a call fails
 * loudly rather than silently mixing allocators across a later free().
 */
#ifndef MSGPACK_MEM_OVERRIDE_H
#define MSGPACK_MEM_OVERRIDE_H

#include <stddef.h>   /* size_t */
#include <stdlib.h>   /* pull the real declarations through before we shadow the names */
#include <string.h>

/* Declared rather than included from fmrb_mem.h: that header drags in the pool
   API and its types, and this file is force-included ahead of everything. */
void *fmrb_sys_malloc(size_t size);
void *fmrb_sys_calloc(size_t nmemb, size_t size);
void *fmrb_sys_realloc(void *ptr, size_t size);
void  fmrb_sys_free(void *ptr);

#define malloc(n)     fmrb_sys_malloc(n)
#define calloc(a, b)  fmrb_sys_calloc((a), (b))
#define realloc(p, n) fmrb_sys_realloc((p), (n))
#define free(p)       fmrb_sys_free(p)

#endif /* MSGPACK_MEM_OVERRIDE_H */
