#ifndef PRISM_CUSTOM_ALLOCATOR_H
#define PRISM_CUSTOM_ALLOCATOR_H

#include <stddef.h>

/*
 * xmalloc/xfree: prism parser internal memory allocation.
 *
 * Target builds (PICORB_VM_MRUBY/MRUBYC): dedicated estalloc pool with mutex.
 * Host build (picorbc): standard libc malloc.
 */
#if defined(MRC_TARGET_MRUBY) || defined(MRC_TARGET_MRUBYC)

  #ifdef __cplusplus
  extern "C" {
  #endif
  extern void* fmrb_prism_malloc(size_t size);
  extern void* fmrb_prism_calloc(size_t nmemb, size_t size);
  extern void* fmrb_prism_realloc(void* ptr, size_t size);
  extern void  fmrb_prism_free(void* ptr);
  #ifdef __cplusplus
  }
  #endif

  #define xmalloc(size)             fmrb_prism_malloc(size)
  #define xcalloc(nmemb,size)       fmrb_prism_calloc(nmemb, size)
  #define xrealloc(ptr,size)        fmrb_prism_realloc(ptr, size)
  #define xfree(ptr)                fmrb_prism_free(ptr)

#else
  /* picorbc (standalone host compiler): use libc */
  #include <stdlib.h>
  #define xmalloc(size)             malloc(size)
  #define xcalloc(nmemb,size)       calloc(nmemb, size)
  #define xrealloc(ptr,size)        realloc(ptr, size)
  #define xfree(ptr)                free(ptr)
#endif

/*
 * mrc_malloc/mrc_free: mruby-compiler2 internal allocation.
 * Uses each VM's allocator via mrc_ccontext. No global state needed.
 */
#if defined(MRC_TARGET_MRUBY)
  #include "mruby.h"
  #define mrc_malloc(c,size)        mrb_malloc(c->mrb, size)
  #define mrc_calloc(c,nmemb,size)  mrb_calloc(c->mrb, nmemb, size)
  #define mrc_realloc(c,ptr,size)   mrb_realloc(c->mrb, ptr, size)
  #define mrc_free(c,ptr)           mrb_free(c->mrb, ptr)
#elif defined(MRC_TARGET_MRUBYC)
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
      if (ptr == NULL) return;
      mrbc_raw_free(ptr);
    }

    static inline void*
    mrc_raw_realloc(void *ptr, unsigned int size)
    {
      if (ptr == NULL) {
        return mrbc_raw_alloc(size);
      } else {
        return mrbc_raw_realloc(ptr, size);
      }
    }
  #endif
#else
<<<<<<< ours
  /* picorbc (standalone host compiler) */
  #include <stdlib.h>
=======

  // for standalone mrbc in PicoRuby
>>>>>>> upstream
  #define mrc_malloc(c,size)        malloc(size)
  #define mrc_calloc(c,nmemb,size)  calloc(nmemb, size)
  #define mrc_realloc(c,ptr,size)   realloc(ptr, size)
  #define mrc_free(c,ptr)           free(ptr)
#endif

#endif
