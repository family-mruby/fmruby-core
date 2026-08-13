/*
 * The gem's entry point, as the mrbgem system wants it.
 *
 * This file exists so the mruby build sees a C source for the gem and emits
 * the mrb_picoruby_fmrb_fft_gem_init call into gem_init.c. The binding itself
 * is in ports/esp32/fft_binding.c, which the picoruby-esp32 component compiles
 * (it needs main/'s headers, which the rake mruby build does not have on its
 * include path) -- the same split picoruby-fmrb-log and -const use.
 */
#include "picoruby.h"

/* ports/esp32/fft_binding.c */
extern void mrb_fmrb_fft_init(mrb_state *mrb);
extern void mrb_fmrb_fft_final(mrb_state *mrb);

void
mrb_picoruby_fmrb_fft_gem_init(mrb_state *mrb)
{
  mrb_fmrb_fft_init(mrb);
}

void
mrb_picoruby_fmrb_fft_gem_final(mrb_state *mrb)
{
  mrb_fmrb_fft_final(mrb);
}
