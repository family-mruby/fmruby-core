#include "picoruby.h"
#include "../include/picoruby_fmrb_debug.h"

// Gem initialization hooks required by the mrbgem system. The real work lives
// in ports/esp32/debug.c (compiled by the CMake component, which can reach the
// main/ debug-core headers).
void
mrb_picoruby_fmrb_debug_gem_init(mrb_state *mrb)
{
  mrb_picoruby_fmrb_debug_init_impl(mrb);
}

void
mrb_picoruby_fmrb_debug_gem_final(mrb_state *mrb)
{
  mrb_picoruby_fmrb_debug_final_impl(mrb);
}
