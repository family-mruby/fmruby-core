#include "picoruby.h"
#include "../include/picoruby_fmrb_filesystem.h"

// Gem initialization functions required by mrbgem system
void
mrb_picoruby_fmrb_filesystem_gem_init(mrb_state *mrb)
{
  mrb_picoruby_fmrb_filesystem_init_impl(mrb);
}

void
mrb_picoruby_fmrb_filesystem_gem_final(mrb_state *mrb)
{
  mrb_picoruby_fmrb_filesystem_final_impl(mrb);
}
