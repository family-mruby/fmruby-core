#include <stdint.h>
#include "picoruby.h"

void
mrb_picoruby_fmrb_kernel_init(mrb_state *mrb)
{
  // Initialize kernel-specific bindings here
  // e.g., process management, IPC, system calls, etc.
}

// Gem initialization functions required by mrbgem system
void
mrb_picoruby_fmrb_kernel_gem_init(mrb_state *mrb)
{
  mrb_picoruby_fmrb_kernel_init(mrb);
}

void
mrb_picoruby_fmrb_kernel_gem_final(mrb_state *mrb)
{
  // Cleanup if needed
}
