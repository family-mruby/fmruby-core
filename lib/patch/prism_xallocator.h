#ifndef PRISM_CUSTOM_ALLOCATOR_H
#define PRISM_CUSTOM_ALLOCATOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// TLSF-based prism allocator (implemented in lib/prism_alloc.c)
// Used by ALL builds: picorbc (host), Linux target, ESP32 target
extern int prism_malloc_init(void);
extern void* prism_malloc(size_t size);
extern void* prism_calloc(size_t nmemb, size_t size);
extern void* prism_realloc(void* ptr, size_t size);
extern void prism_free(void* ptr);

#ifdef __cplusplus
}
#endif

// Map xmalloc to prism_malloc for ALL builds (unified TLSF allocator)
#define xmalloc(size)             prism_malloc(size)
#define xcalloc(nmemb,size)       prism_calloc(nmemb, size)
#define xrealloc(ptr,size)        prism_realloc(ptr, size)
#define xfree(ptr)                prism_free(ptr)

// mrc_* functions depend on target runtime
#if defined(MRC_TARGET_MRUBY)
  #include "mruby.h"
  #define mrc_malloc(c,size)        mrb_malloc(c->mrb, size)
  #define mrc_calloc(c,nmemb,size)  mrb_calloc(c->mrb, nmemb, size)
  #define mrc_realloc(c,ptr,size)   mrb_realloc(c->mrb, ptr, size)
  #define mrc_free(c,ptr)           mrb_free(c->mrb, ptr)
#elif defined(MRC_TARGET_MRUBYC)
  // mrubyc definitions
  #include "mrubyc.h"
  #if defined(MRBC_ALLOC_LIBC)
    #define mrc_malloc(c,size)        malloc(size)
    #define mrc_calloc(c,nmemb,size)  calloc(nmemb, size)
    #define mrc_realloc(c,ptr,size)   realloc(ptr, size)
    #define mrc_free(c,ptr)           free(ptr)
  #else
    #define mrc_malloc(c,size)        mrbc_raw_alloc(size)
    #define mrc_calloc(c,nmemb,size)  mrbc_raw_calloc(nmemb, size)
    #define mrc_realloc(c,ptr,size)   mrc_raw_realloc(ptr, size)
    #define mrc_free(c,ptr)           mrc_raw_free(ptr)

    static inline void mrc_raw_free(void *ptr)
    {
      /* mrbc_raw_free() warns when ptr=NULL but it should be allowed in C99 */
      if (ptr == NULL) return;
      mrbc_raw_free(ptr);
    }

    static inline void*
    mrc_raw_realloc(void *ptr, unsigned int size)
    {
      /* mrbc_raw_realloc() fails when ptr=NULL but it should be allowed in C99 */
      if (ptr == NULL) {
        return mrbc_raw_alloc(size);
      } else {
        return mrbc_raw_realloc(ptr, size);
      }
    }
  #endif
#else
  // For picorbc and other builds: use standard malloc for mrc_* functions
  #include <stdlib.h>
  #define mrc_malloc(c,size)        malloc(size)
  #define mrc_calloc(c,nmemb,size)  calloc(nmemb, size)
  #define mrc_realloc(c,ptr,size)   realloc(ptr, size)
  #define mrc_free(c,ptr)           free(ptr)
#endif

#endif
