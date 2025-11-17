// Debug-enhanced version of cipush() for mruby vm.c
// This file should replace the cipush() function in vm.c
// Location: components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/src/vm.c

static inline mrb_callinfo*
cipush(mrb_state *mrb, mrb_int push_stacks, uint8_t cci, struct RClass *target_class,
       const struct RProc *proc, struct RProc *blk, mrb_sym mid, uint16_t argc)
{
  struct mrb_context *c = mrb->c;
  mrb_callinfo *ci = c->ci + 1;

  if (ci < c->ciend) {
    c->ci = ci;
  }
  else {
    ptrdiff_t size = ci - c->cibase;

    if (size >= MRB_CALL_LEVEL_MAX) {
      mrb_exc_raise(mrb, mrb_obj_value(mrb->stack_err));
    }

    // *** DEBUG LOG: Before realloc ***
    mrb_callinfo *old_cibase = c->cibase;
    mrb_callinfo *old_ciend = c->ciend;
    mrb_callinfo *old_ci = c->ci;

#ifdef FMRB_DEBUG_CI_REALLOC
    fprintf(stderr, "[CIPUSH REALLOC] BEFORE: cibase=%p ci=%p ciend=%p size=%td new_size=%td\n",
            old_cibase, old_ci, old_ciend, size, size*2);
#endif

    c->cibase = (mrb_callinfo*)mrb_realloc(mrb, c->cibase, sizeof(mrb_callinfo)*size*2);
    c->ci = ci = c->cibase + size;
    c->ciend = c->cibase + size * 2;

    // *** DEBUG LOG: After realloc ***
#ifdef FMRB_DEBUG_CI_REALLOC
    fprintf(stderr, "[CIPUSH REALLOC] AFTER:  cibase=%p ci=%p ciend=%p (moved=%s)\n",
            c->cibase, c->ci, c->ciend,
            (c->cibase != old_cibase) ? "YES" : "NO");
    if(c->cibase != old_cibase) {
      fprintf(stderr, "[CIPUSH REALLOC] WARNING: cibase moved from %p to %p (delta=%td bytes)\n",
              old_cibase, c->cibase, (char*)c->cibase - (char*)old_cibase);
    }
#endif
  }

  ci->mid = mid;
  CI_PROC_SET(ci, proc);
  ci->blk = blk;
  ci->stack = ci[-1].stack + push_stacks;
  ci->n = argc & 0xf;
  ci->nk = (argc>>4) & 0xf;
  ci->cci = cci;
  ci->vis = MRB_METHOD_PUBLIC_FL;
  ci->u.target_class = target_class;

  return ci;
}
