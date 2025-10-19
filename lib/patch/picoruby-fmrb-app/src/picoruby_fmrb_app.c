#include "picoruby.h"

void
mrb_picoruby_fmrb_init(mrb_state *mrb)
{

}

// C API functions for event dispatch
// These are called from the C layer (kernel, system task, etc.)

// Gem initialization functions required by mrbgem system
void
mrb_picoruby_fmrb_app_gem_init(mrb_state *mrb)
{
  mrb_picoruby_fmrb_init(mrb);
}

void
mrb_picoruby_fmrb_app_gem_final(mrb_state *mrb)
{
  // Cleanup if needed
}
