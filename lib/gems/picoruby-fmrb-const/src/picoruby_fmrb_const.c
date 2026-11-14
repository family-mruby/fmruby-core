#include "picoruby.h"
#include "../include/picoruby_fmrb_const.h"

// Standard mrbgem initialization function called by gem_init.c
void
mrb_picoruby_fmrb_const_gem_init(mrb_state *mrb)
{
  mrb_picoruby_fmrb_const_init_impl(mrb);
}

// Standard mrbgem finalization function
void
mrb_picoruby_fmrb_const_gem_final(mrb_state *mrb)
{
  // No cleanup needed for constants
}
