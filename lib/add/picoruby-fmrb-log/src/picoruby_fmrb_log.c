#include "picoruby.h"

// Forward declaration for ESP32 port initialization
extern void mrb_fmrb_log_init(mrb_state *mrb);

void
mrb_picoruby_fmrb_log_init(mrb_state *mrb)
{
  // Delegate to platform-specific implementation
  mrb_fmrb_log_init(mrb);
}

// Gem initialization functions required by mrbgem system
void
mrb_picoruby_fmrb_log_gem_init(mrb_state *mrb)
{
  mrb_picoruby_fmrb_log_init(mrb);
}

void
mrb_picoruby_fmrb_log_gem_final(mrb_state *mrb)
{
  // Cleanup if needed
}
