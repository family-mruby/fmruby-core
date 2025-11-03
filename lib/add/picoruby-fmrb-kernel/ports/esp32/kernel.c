#include <mruby.h>
#include <mruby/value.h>
#include <mruby/class.h>
#include "fmrb_kernel.h"

// FmrbKernel#_set_ready()
static mrb_value mrb_kernel_set_ready(mrb_state *mrb, mrb_value self)
{
    fmrb_kernel_set_ready();
    return mrb_nil_value();
}

void mrb_fmrb_kernel_init(mrb_state *mrb)
{
    struct RClass *kernel_class = mrb_define_class(mrb, "FmrbKernel", mrb->object_class);

    mrb_define_method(mrb, kernel_class, "_set_ready", mrb_kernel_set_ready, MRB_ARGS_NONE());
}

void mrb_fmrb_kernel_final(mrb_state *mrb)
{
    // Cleanup if needed
}
