/* sp_mem_override.h -- allocation interception for the SP_MULTI_CTX build.
 *
 * Force-injected into every SP_MULTI_CTX translation unit via
 * `-include lib/sp_mem_override.h` (the Makefile mc rules). It is NEVER included
 * in the default build, so the default sources and objects stay byte-identical.
 *
 * Rather than rewrite the ~490 malloc/calloc/realloc/free/strdup call sites
 * across the runtime (error-prone and hostile to upstream merges), we remap the
 * libc names to per-instance wrappers with function-like macros -- the same
 * "errno-style" indirection Phase 3 used for the runtime globals. Every
 * allocation in the runtime .c files (lib and regexp), the sp_runtime.h inlines,
 * and the generated program TU (which includes sp_runtime.h) then routes through
 * the current
 * instance's sp_ctx backend, so instances no longer share one libc heap.
 *
 * The wrappers are DEFINED once, in sp_ctx.c, which #undefs these macros at the
 * top so its bodies reach the real libc. No other TU may #undef them.
 *
 * Boundary -- intentionally NOT hooked: stdio internal buffers (fopen / printf /
 * getline ...) allocate inside libc and go to the system heap; see
 * docs/internals/multi-instance.md. Only the call form `name(` is remapped, so a
 * bare function-pointer reference to `free` (none exist today) would stay libc,
 * and the nm gate (`make check-mc-syms`) would catch it.
 */
#ifndef SP_MEM_OVERRIDE_H
#define SP_MEM_OVERRIDE_H

#include <stddef.h>   /* size_t */
#include <stdlib.h>   /* pull the real declarations through before we shadow the names */
#include <string.h>

void *sp_mem_malloc(size_t n);
void *sp_mem_calloc(size_t nmemb, size_t size);
void *sp_mem_realloc(void *p, size_t n);
void  sp_mem_free(void *p);
char *sp_mem_strdup(const char *s);

#define malloc(n)     sp_mem_malloc(n)
#define calloc(a, b)  sp_mem_calloc((a), (b))
#define realloc(p, n) sp_mem_realloc((p), (n))
#define free(p)       sp_mem_free(p)
#define strdup(s)     sp_mem_strdup(s)

#endif /* SP_MEM_OVERRIDE_H */
