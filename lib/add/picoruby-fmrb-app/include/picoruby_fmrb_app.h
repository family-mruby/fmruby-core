#pragma once

#include <mruby.h>

void mrb_picoruby_fmrb_app_init_impl(mrb_state *mrb);
void mrb_picoruby_fmrb_app_final_impl(mrb_state *mrb);

// C API: Cleanup VM resources after script execution completes
void fmrb_app_vm_cleanup(mrb_state *mrb);
