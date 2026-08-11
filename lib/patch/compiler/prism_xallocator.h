#ifndef PRISM_CUSTOM_ALLOCATOR_H
#define PRISM_CUSTOM_ALLOCATOR_H

#if defined(MRC_TARGET_MRUBY)
  #include "mruby.h"

  #if defined(MRC_ALLOC_LIBC)
    #define xmalloc(size)             malloc(size)
    #define xcalloc(nmemb,size)       calloc(nmemb, size)
    #define xrealloc(nmemb,size)      realloc(nmemb, size)
    #define xfree(ptr)                free(ptr)

    #define mrc_malloc(c,size)        malloc(size)
    #define mrc_calloc(c,nmemb,size)  calloc(nmemb, size)
    #define mrc_realloc(c,ptr,size)   realloc(ptr, size)
    #define mrc_free(c,ptr)           free(ptr)
  #else
    extern mrb_state *global_mrb;

    /* family-mruby: prism allocates through these hooks instead of straight
       through global_mrb. global_mrb is a single process-wide pointer, set to
       whichever VM last opened a compile context, and Family mruby runs one VM
       per app on its own task -- plus tasks with no VM at all (Spinel apps).
       Calling prism from such a task through global_mrb means writing GC
       bookkeeping into a foreign VM, running its collector from the wrong task,
       longjmp-ing into its jmpbuf on OOM, or dereferencing NULL before any app
       has compiled. The hooks pick the calling task's own VM instead; see
       mruby-compiler2-ccontext.c for what they do and doc/editor_ti/report/p2.md
       for how this was found. */
    void *fmrb_prism_realloc(void *ptr, size_t size);
    void *fmrb_prism_calloc(size_t nmemb, size_t size);

    #define xmalloc(size)             fmrb_prism_realloc(NULL, (size))
    #define xcalloc(nmemb,size)       fmrb_prism_calloc((nmemb), (size))
    #define xrealloc(ptr,size)        fmrb_prism_realloc((ptr), (size))
    #define xfree(ptr)                ((void)fmrb_prism_realloc((ptr), 0))

    #define mrc_malloc(c,size)        mrb_malloc(c->mrb, size)
    #define mrc_calloc(c,nmemb,size)  mrb_calloc(c->mrb, nmemb, size)
    #define mrc_realloc(c,ptr,size)   mrb_realloc(c->mrb, ptr, size)
    #define mrc_free(c,ptr)           mrb_free(c->mrb, ptr)
  #endif
#elif defined(MRC_TARGET_MRUBYC)
  #include "mrubyc.h"
  #if defined(MRBC_ALLOC_LIBC)
    #define xmalloc(size)             malloc(size)
    #define xcalloc(nmemb,size)       calloc(nmemb, size)
    #define xrealloc(nmemb,size)      realloc(nmemb, size)
    #define xfree(ptr)                free(ptr)

    #define mrc_malloc(c,size)        malloc(size)
    #define mrc_calloc(c,nmemb,size)  calloc(nmemb, size)
    #define mrc_realloc(c,ptr,size)   realloc(ptr, size)
    #define mrc_free(c,ptr)           free(ptr)
  #else
    #define xmalloc(size)             mrbc_raw_alloc(size)
    #define xcalloc(nmemb,size)       mrbc_raw_calloc(nmemb, size)
    #define xrealloc(nmemb,size)      mrc_raw_realloc(nmemb, size)
    #define xfree(ptr)                mrc_raw_free(ptr)

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

  // for standalone mrbc in PicoRuby
  #define mrc_malloc(c,size)        malloc(size)
  #define mrc_calloc(c,nmemb,size)  calloc(nmemb, size)
  #define mrc_realloc(c,ptr,size)   realloc(ptr, size)
  #define mrc_free(c,ptr)           free(ptr)
  #define xmalloc(size)             malloc(size)
  #define xcalloc(nmemb,size)       calloc(nmemb, size)
  #define xrealloc(ptr,size)        realloc(ptr, size)
  #define xfree(ptr)                free(ptr)

#endif

#endif
