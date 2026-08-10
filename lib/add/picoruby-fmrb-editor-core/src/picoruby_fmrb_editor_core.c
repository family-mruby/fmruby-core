#include "picoruby.h"
#include "../include/picoruby_fmrb_editor_core.h"

// Gem hooks. The document model itself is in ports/esp32/editor_core.c, which
// the CMake component compiles with the firmware include paths.
void
mrb_picoruby_fmrb_editor_core_gem_init(mrb_state *mrb)
{
  mrb_picoruby_fmrb_editor_core_init_impl(mrb);
}

void
mrb_picoruby_fmrb_editor_core_gem_final(mrb_state *mrb)
{
  mrb_picoruby_fmrb_editor_core_final_impl(mrb);
}
