#include "picoruby.h"
#include "../../main/lib/fmrb_hal/fmrb_hal.h"

static mrb_value
fmrb_hal_init_wrapper(mrb_state *mrb, mrb_value self)
{
  fmrb_err_t ret = fmrb_hal_init();
  return mrb_fixnum_value(ret);
}

static mrb_value
fmrb_hal_deinit_wrapper(mrb_state *mrb, mrb_value self)
{
  fmrb_hal_deinit();
  return mrb_nil_value();
}

static mrb_value
fmrb_hal_time_get_ms_wrapper(mrb_state *mrb, mrb_value self)
{
  uint64_t time_ms = fmrb_hal_time_get_ms();
  return mrb_fixnum_value(time_ms);
}

static mrb_value
fmrb_hal_time_delay_ms_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int ms;
  mrb_get_args(mrb, "i", &ms);

  if (ms < 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "delay time must be positive");
  }

  fmrb_hal_time_delay_ms((uint32_t)ms);
  return mrb_nil_value();
}

static mrb_value
fmrb_hal_gpio_config_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int gpio_num, mode, pull;
  mrb_get_args(mrb, "iii", &gpio_num, &mode, &pull);

  fmrb_err_t ret = fmrb_hal_gpio_config(gpio_num, mode, pull);
  return mrb_fixnum_value(ret);
}

static mrb_value
fmrb_hal_gpio_set_level_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int gpio_num, level;
  mrb_get_args(mrb, "ii", &gpio_num, &level);

  fmrb_err_t ret = fmrb_hal_gpio_set_level(gpio_num, level);
  return mrb_fixnum_value(ret);
}

static mrb_value
fmrb_hal_gpio_get_level_wrapper(mrb_state *mrb, mrb_value self)
{
  mrb_int gpio_num;
  mrb_get_args(mrb, "i", &gpio_num);

  int level = fmrb_hal_gpio_get_level(gpio_num);
  return mrb_fixnum_value(level);
}

void
mrb_picoruby_fmrb_hal_init(mrb_state *mrb)
{
  struct RClass *fmrb_hal_class = mrb_define_class(mrb, "FmrbHAL", mrb->object_class);

  // HAL module methods
  mrb_define_class_method(mrb, fmrb_hal_class, "init", fmrb_hal_init_wrapper, MRB_ARGS_NONE());
  mrb_define_class_method(mrb, fmrb_hal_class, "deinit", fmrb_hal_deinit_wrapper, MRB_ARGS_NONE());

  // Time methods
  mrb_define_class_method(mrb, fmrb_hal_class, "time_ms", fmrb_hal_time_get_ms_wrapper, MRB_ARGS_NONE());
  mrb_define_class_method(mrb, fmrb_hal_class, "delay_ms", fmrb_hal_time_delay_ms_wrapper, MRB_ARGS_REQ(1));

  // GPIO methods
  mrb_define_class_method(mrb, fmrb_hal_class, "gpio_config", fmrb_hal_gpio_config_wrapper, MRB_ARGS_REQ(3));
  mrb_define_class_method(mrb, fmrb_hal_class, "gpio_set", fmrb_hal_gpio_set_level_wrapper, MRB_ARGS_REQ(2));
  mrb_define_class_method(mrb, fmrb_hal_class, "gpio_get", fmrb_hal_gpio_get_level_wrapper, MRB_ARGS_REQ(1));

  // Constants
  mrb_define_const(mrb, fmrb_hal_class, "OK", mrb_fixnum_value(FMRB_OK));
  mrb_define_const(mrb, fmrb_hal_class, "ERR_INVALID_PARAM", mrb_fixnum_value(FMRB_ERR_INVALID_PARAM));
  mrb_define_const(mrb, fmrb_hal_class, "ERR_NO_MEMORY", mrb_fixnum_value(FMRB_ERR_NO_MEMORY));
  mrb_define_const(mrb, fmrb_hal_class, "ERR_TIMEOUT", mrb_fixnum_value(FMRB_ERR_TIMEOUT));
  mrb_define_const(mrb, fmrb_hal_class, "ERR_NOT_SUPPORTED", mrb_fixnum_value(FMRB_ERR_NOT_SUPPORTED));
  mrb_define_const(mrb, fmrb_hal_class, "ERR_BUSY", mrb_fixnum_value(FMRB_ERR_BUSY));
  mrb_define_const(mrb, fmrb_hal_class, "ERR_FAILED", mrb_fixnum_value(FMRB_ERR_FAILED));

  // GPIO constants
  mrb_define_const(mrb, fmrb_hal_class, "GPIO_MODE_INPUT", mrb_fixnum_value(FMRB_GPIO_MODE_INPUT));
  mrb_define_const(mrb, fmrb_hal_class, "GPIO_MODE_OUTPUT", mrb_fixnum_value(FMRB_GPIO_MODE_OUTPUT));
  mrb_define_const(mrb, fmrb_hal_class, "GPIO_MODE_OUTPUT_OD", mrb_fixnum_value(FMRB_GPIO_MODE_OUTPUT_OD));

  mrb_define_const(mrb, fmrb_hal_class, "GPIO_PULL_NONE", mrb_fixnum_value(FMRB_GPIO_PULL_NONE));
  mrb_define_const(mrb, fmrb_hal_class, "GPIO_PULL_UP", mrb_fixnum_value(FMRB_GPIO_PULL_UP));
  mrb_define_const(mrb, fmrb_hal_class, "GPIO_PULL_DOWN", mrb_fixnum_value(FMRB_GPIO_PULL_DOWN));
}