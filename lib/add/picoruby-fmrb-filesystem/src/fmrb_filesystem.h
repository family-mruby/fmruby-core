/*
 * FMRuby Filesystem - Header
 */

#ifndef FMRB_FILESYSTEM_H
#define FMRB_FILESYSTEM_H

#include <mruby.h>

#ifdef __cplusplus
extern "C" {
#endif

// Gem initialization functions
void mrb_mruby_fmrb_filesystem_gem_init(mrb_state *mrb);
void mrb_mruby_fmrb_filesystem_gem_final(mrb_state *mrb);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_FILESYSTEM_H */
