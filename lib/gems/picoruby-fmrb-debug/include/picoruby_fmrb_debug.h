#pragma once

#include <mruby.h>

// The Ruby-facing bindings (FMRB::Debug) live in ports/esp32/debug.c because
// they call the main/ debug core (fmrb_debug_ctx / fmrb_debugd), whose headers
// are only visible to the CMake-built component. src/ (rake-built) just wires
// the mrbgem init hooks to these.
void mrb_picoruby_fmrb_debug_init_impl(mrb_state *mrb);
void mrb_picoruby_fmrb_debug_final_impl(mrb_state *mrb);
