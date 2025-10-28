#include "picoruby.h"
#include "../include/fmrb_app.h"

// Gem initialization functions required by mrbgem system
void
mrb_picoruby_fmrb_app_gem_init(mrb_state *mrb)
{
  mrb_picoruby_fmrb_app_init_impl(mrb);
}

void
mrb_picoruby_fmrb_app_gem_final(mrb_state *mrb)
{
  mrb_picoruby_fmrb_app_final_impl(mrb);
}
