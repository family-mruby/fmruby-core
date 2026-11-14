/*
** file_ext.c - Extensions to mruby-io File class
**
** family-mruby: fsync is not needed (fmrb-io does not use mruby-io).
** This stub replaces the upstream version that depends on mruby-io.
*/

#include <mruby.h>

void
mrb_file_ext_init(mrb_state *mrb)
{
  (void)mrb;
}
