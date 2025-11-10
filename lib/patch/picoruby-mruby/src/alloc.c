#include <string.h>

#include "mruby.h"
#include "mruby/presym.h"
#include "mruby/hash.h"

extern void* fmrb_get_current_est(void);
extern void  fmrb_set_current_est(void*);

// PICORB_ALLOC_TINYALLOC
//#elif defined(PICORB_ALLOC_ESTALLOC)

#include "../lib/estalloc/estalloc.h"

static ESTALLOC *g_est = NULL;

void *
//mrb_estalloc_allocf(mrb_state *mrb, void *p, size_t size, void *est)
mrb_basic_alloc_func(void* ptr, size_t size)
{
  ESTALLOC *est = (ESTALLOC*)fmrb_get_current_est();
  if (size == 0) {
    /* `free(NULL)` should be no-op */
    est_free(est, ptr);
    return NULL;
  }
  /* `ralloc(NULL, size)` works as `malloc(size)` */
  return est_realloc(est, ptr, size);
}

mrb_value
mrb_alloc_statistics(mrb_state *mrb)
{
  ESTALLOC *est = (ESTALLOC*)fmrb_get_current_est();
  est_take_statistics(est);
  ESTALLOC_STAT *stat = &est->stat;
  mrb_value hash = mrb_hash_new_capa(mrb, 5);
  mrb_hash_set(mrb, hash, mrb_symbol_value(MRB_SYM(allocator)), mrb_symbol_value(MRB_SYM(ESTALLOC)));
  mrb_hash_set(mrb, hash, mrb_symbol_value(MRB_SYM(total)), mrb_fixnum_value(stat->total));
  mrb_hash_set(mrb, hash, mrb_symbol_value(MRB_SYM(used)), mrb_fixnum_value(stat->used));
  mrb_hash_set(mrb, hash, mrb_symbol_value(MRB_SYM(free)), mrb_fixnum_value(stat->free));
  mrb_hash_set(mrb, hash, mrb_symbol_value(MRB_SYM(frag)), mrb_fixnum_value(stat->frag));
  return hash;
}

mrb_state *
mrb_open_with_custom_alloc(void* mem, size_t bytes)
{
  ESTALLOC *est = est_init(mem, bytes);
  fmrb_set_current_est(est);
  return mrb_open();
}

// PICORB_ALLOC_ESTALLOC
//#endif
