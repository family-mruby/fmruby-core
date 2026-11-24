/* A fixture: the forms of mrb_define_* the C counter follows. */
#include <mruby.h>

static struct RClass *g_widget;

void
mrb_demo_gem_init(mrb_state *mrb)
{
  struct RClass *foo = mrb_define_class(mrb, "Foo", mrb->object_class);
  struct RClass *kernel = mrb->kernel_module;
  mrb_define_method(mrb, foo, "bar", demo_bar, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, foo, MRB_SYM_Q(ready), demo_ready, MRB_ARGS_NONE());
  mrb_define_class_method(mrb, foo, "make", demo_make, MRB_ARGS_NONE());
  mrb_define_method(mrb, foo, "initialize", demo_init, MRB_ARGS_NONE());
  mrb_define_method(mrb, foo, "_raw", demo_raw, MRB_ARGS_NONE());
  g_widget = mrb_define_class_under(mrb, foo, "Widget", mrb->object_class);
  mrb_define_method(mrb, g_widget, "spin", demo_spin, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, mrb_define_module(mrb, "Tool"), "c_go", demo_go, MRB_ARGS_NONE());
  mrb_define_method(mrb, kernel, "beep", demo_beep, MRB_ARGS_NONE());
  /* mrb_define_method(mrb, foo, "commented", demo_x, MRB_ARGS_NONE()); */
  // mrb_define_method(mrb, foo, "line_commented", demo_x, MRB_ARGS_NONE());
  mrb_define_method(mrb, helper_class(mrb), "lost", demo_lost, MRB_ARGS_NONE());
}
