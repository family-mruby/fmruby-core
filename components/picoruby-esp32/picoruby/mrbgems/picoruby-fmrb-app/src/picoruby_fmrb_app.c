#include "picoruby.h"

// Forward declarations
void mrb_picoruby_fmrb_hal_init(mrb_state *mrb);
void mrb_picoruby_fmrb_gfx_init(mrb_state *mrb);
void mrb_picoruby_fmrb_audio_init(mrb_state *mrb);

// Global app instance for event dispatch
static mrb_value g_app_instance = mrb_nil_value();
static mrb_state *g_mrb = NULL;

void
mrb_picoruby_fmrb_init(mrb_state *mrb)
{
  g_mrb = mrb;

  // Initialize all FMRB modules
  mrb_picoruby_fmrb_hal_init(mrb);
  mrb_picoruby_fmrb_gfx_init(mrb);
  mrb_picoruby_fmrb_audio_init(mrb);
}

// C API functions for event dispatch
// These are called from the C layer (kernel, system task, etc.)

void
fmrb_app_set_instance(mrb_state *mrb, mrb_value app_instance)
{
  g_app_instance = app_instance;
  g_mrb = mrb;
}

mrb_value
fmrb_app_get_instance(void)
{
  return g_app_instance;
}

int
fmrb_app_dispatch_update(uint32_t delta_time_ms)
{
  if (g_mrb == NULL || mrb_nil_p(g_app_instance)) {
    return -1;
  }

  mrb_value args[1];
  args[0] = mrb_fixnum_value(delta_time_ms);

  mrb_funcall_argv(g_mrb, g_app_instance, mrb_intern_cstr(g_mrb, "update"), 1, args);

  if (g_mrb->exc) {
    mrb_print_error(g_mrb);
    g_mrb->exc = NULL;
    return -1;
  }

  return 0;
}

int
fmrb_app_dispatch_key_down(int key_code)
{
  if (g_mrb == NULL || mrb_nil_p(g_app_instance)) {
    return -1;
  }

  mrb_value args[1];
  args[0] = mrb_fixnum_value(key_code);

  mrb_funcall_argv(g_mrb, g_app_instance, mrb_intern_cstr(g_mrb, "dispatch_key_down"), 1, args);

  if (g_mrb->exc) {
    mrb_print_error(g_mrb);
    g_mrb->exc = NULL;
    return -1;
  }

  return 0;
}

int
fmrb_app_dispatch_key_up(int key_code)
{
  if (g_mrb == NULL || mrb_nil_p(g_app_instance)) {
    return -1;
  }

  mrb_value args[1];
  args[0] = mrb_fixnum_value(key_code);

  mrb_funcall_argv(g_mrb, g_app_instance, mrb_intern_cstr(g_mrb, "dispatch_key_up"), 1, args);

  if (g_mrb->exc) {
    mrb_print_error(g_mrb);
    g_mrb->exc = NULL;
    return -1;
  }

  return 0;
}

int
fmrb_app_dispatch_mouse_move(int x, int y)
{
  if (g_mrb == NULL || mrb_nil_p(g_app_instance)) {
    return -1;
  }

  mrb_value args[2];
  args[0] = mrb_fixnum_value(x);
  args[1] = mrb_fixnum_value(y);

  mrb_funcall_argv(g_mrb, g_app_instance, mrb_intern_cstr(g_mrb, "dispatch_mouse_move"), 2, args);

  if (g_mrb->exc) {
    mrb_print_error(g_mrb);
    g_mrb->exc = NULL;
    return -1;
  }

  return 0;
}

int
fmrb_app_dispatch_mouse_click(int x, int y, int button)
{
  if (g_mrb == NULL || mrb_nil_p(g_app_instance)) {
    return -1;
  }

  mrb_value args[3];
  args[0] = mrb_fixnum_value(x);
  args[1] = mrb_fixnum_value(y);
  args[2] = mrb_fixnum_value(button);

  mrb_funcall_argv(g_mrb, g_app_instance, mrb_intern_cstr(g_mrb, "dispatch_mouse_click"), 3, args);

  if (g_mrb->exc) {
    mrb_print_error(g_mrb);
    g_mrb->exc = NULL;
    return -1;
  }

  return 0;
}