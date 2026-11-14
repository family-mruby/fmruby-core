/*
 * The gem's entry point, as the mrbgem system wants it.
 *
 * This file exists so the mruby build sees a C source for the gem and emits the
 * mrb_picoruby_fmrb_raycast_gem_init call into gem_init.c. The binding itself
 * is in ports/esp32/raycast_binding.c, which the picoruby-esp32 component
 * compiles (it needs main/'s headers) -- the same split the other fmrb gems
 * use. Without this file the gem's mrblib and init go unregistered and the app
 * dies on NameError.
 */
#include "picoruby.h"

/* ports/esp32/raycast_binding.c */
extern void mrb_fmrb_raycast_init(mrb_state *mrb);
extern void mrb_fmrb_raycast_final(mrb_state *mrb);

void
mrb_picoruby_fmrb_raycast_gem_init(mrb_state *mrb)
{
  mrb_fmrb_raycast_init(mrb);
}

void
mrb_picoruby_fmrb_raycast_gem_final(mrb_state *mrb)
{
  mrb_fmrb_raycast_final(mrb);
}
