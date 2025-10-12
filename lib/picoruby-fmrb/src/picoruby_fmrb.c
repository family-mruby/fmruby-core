#include "picoruby.h"

// Forward declarations
void mrb_picoruby_fmrb_hal_init(mrb_state *mrb);
void mrb_picoruby_fmrb_gfx_init(mrb_state *mrb);
void mrb_picoruby_fmrb_audio_init(mrb_state *mrb);

void
mrb_picoruby_fmrb_init(mrb_state *mrb)
{
  // Initialize all FMRB modules
  mrb_picoruby_fmrb_hal_init(mrb);
  mrb_picoruby_fmrb_gfx_init(mrb);
  mrb_picoruby_fmrb_audio_init(mrb);
}