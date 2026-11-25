#include <mruby.h>
#include <mruby/presym.h>
#include <mruby/variable.h>
#include <mruby/string.h>

#define IVPINNUM()    mrb_integer(mrb_iv_get(mrb, self, MRB_IVSYM(pin)))

/*
 * Family mruby: every set-up and write asks the port first (GPIO_claim), and
 * a refusal raises before the pin is touched. A read that fails raises too,
 * instead of returning -1 (which high? took for high).
 */
static void
gpio_raise_claim(mrb_state *mrb, int pin, int claim)
{
  struct RClass *io_error = mrb_exc_get_id(mrb, MRB_SYM(IOError));
  switch (claim) {
    case GPIO_CLAIM_OK:
    case GPIO_CLAIM_OK_SHARED:
      return;
    case GPIO_CLAIM_NO_RANGE:
      mrb_raisef(mrb, E_ARGUMENT_ERROR, "GPIO %d does not exist on this board", pin);
      break;
    case GPIO_CLAIM_NO_SYSTEM:
      mrb_raisef(mrb, io_error, "GPIO %d is used by the system", pin);
      break;
    case GPIO_CLAIM_INPUT_ONLY:
      mrb_raisef(mrb, io_error, "GPIO %d is a system button: input only", pin);
      break;
    case GPIO_CLAIM_IN_USE:
      mrb_raisef(mrb, io_error, "GPIO %d is in use by another app or driver", pin);
      break;
    default:
      mrb_raisef(mrb, io_error, "GPIO %d could not be set up", pin);
      break;
  }
}

static void
gpio_claim(mrb_state *mrb, int pin, int use)
{
  gpio_raise_claim(mrb, pin, GPIO_claim(pin, use, 0));
}

/* What a set_dir / GPIO.new flags value asks of the pin. */
static int
gpio_use_of_flags(int flags)
{
  return (flags & (OUT | OPEN_DRAIN | ALT)) ? GPIO_USE_OUTPUT : GPIO_USE_INPUT;
}

static int
gpio_read_checked(mrb_state *mrb, int pin)
{
  int level = GPIO_read(pin);
  if (level < 0) {
    mrb_raisef(mrb, mrb_exc_get_id(mrb, MRB_SYM(IOError)), "GPIO %d could not be read", pin);
  }
  return level;
}

static int
pin_num(mrb_state *mrb, int *opt_value)
{
  mrb_value pin;
  int pin_number;
  if (opt_value) {
    mrb_get_args(mrb, "oi", &pin, opt_value);
  } else {
    mrb_get_args(mrb, "o", &pin);
  }
  switch (mrb_type(pin)) {
    case MRB_TT_INTEGER: {
      pin_number = mrb_integer(pin);
      break;
    }
    case MRB_TT_STRING: {
      pin_number = GPIO_pin_num_from_char((const uint8_t *)RSTRING_PTR(pin));
      break;
    }
    case MRB_TT_SYMBOL: {
      pin_number = GPIO_pin_num_from_char((const uint8_t *)mrb_sym_name(mrb, mrb_symbol(pin)));
      break;
    }
    default:
      pin_number = -1;
  }
  if (pin_number < 0) {
    mrb_raisef(mrb, E_ARGUMENT_ERROR, "Wrong GPIO pin name: %v", pin);
  }
  return pin_number;
}

/*
 * GPIO._init(pin, flags)
 *
 * Takes the pin for this app as the flags ask (input or output) and resets
 * it, or raises without touching it.
 */
static mrb_value
mrb__init(mrb_state *mrb, mrb_value self)
{
  int flags;
  int pin_number = pin_num(mrb, &flags);
  gpio_raise_claim(mrb, pin_number, GPIO_claim(pin_number, gpio_use_of_flags(flags), 1));
  return mrb_fixnum_value(0);
}

/*
 * GPIO.set_function_at(pin, function)
 */
static mrb_value
mrb_s_set_function_at(mrb_state *mrb, mrb_value klass)
{
  int pin_number, alt_function;
  pin_number = pin_num(mrb, &alt_function);
  gpio_claim(mrb, pin_number, GPIO_USE_OUTPUT);
  GPIO_set_function(pin_number, alt_function);
  return mrb_fixnum_value(0);
}


/*
 * GPIO.set_dir_at(pin, dir)
 */
static mrb_value
mrb_s_set_dir_at(mrb_state *mrb, mrb_value klass)
{
  int pin_number, flags;
  pin_number = pin_num(mrb, &flags);
  gpio_claim(mrb, pin_number, gpio_use_of_flags(flags));
  GPIO_set_dir(pin_number, flags);
  return mrb_fixnum_value(0);
}

/*
 * GPIO.open_drain_at(pin)
 */
static mrb_value
mrb_s_open_drain_at(mrb_state *mrb, mrb_value klass)
{
  int pin_number = pin_num(mrb, NULL);
  gpio_claim(mrb, pin_number, GPIO_USE_OUTPUT);
  GPIO_open_drain(pin_number);
  return mrb_fixnum_value(0);
}

/*
 * GPIO.pull_up_at(pin)
 */
static mrb_value
mrb_s_pull_up_at(mrb_state *mrb, mrb_value klass)
{
  int pin_number = pin_num(mrb, NULL);
  gpio_claim(mrb, pin_number, GPIO_USE_INPUT);
  GPIO_pull_up(pin_number);
  return mrb_fixnum_value(0);
}

/*
 * GPIO.pull_down_at(pin)
 */
static mrb_value
mrb_s_pull_down_at(mrb_state *mrb, mrb_value klass)
{
  int pin_number = pin_num(mrb, NULL);
  gpio_claim(mrb, pin_number, GPIO_USE_INPUT);
  GPIO_pull_down(pin_number);
  return mrb_fixnum_value(0);
}

/*
 * GPIO.high_at?(pin)
 */
static mrb_value
mrb_s_high_at_p(mrb_state *mrb, mrb_value klass)
{
  int pin_number = pin_num(mrb, NULL);
  gpio_claim(mrb, pin_number, GPIO_USE_READ);
  if (gpio_read_checked(mrb, pin_number) == 0) {
    return mrb_false_value();
  } else {
    return mrb_true_value();
  }
}

/*
 * GPIO.low_at?(pin)
 */
static mrb_value
mrb_s_low_at_p(mrb_state *mrb, mrb_value klass)
{
  int pin_number = pin_num(mrb, NULL);
  gpio_claim(mrb, pin_number, GPIO_USE_READ);
  if (gpio_read_checked(mrb, pin_number) == 0) {
    return mrb_true_value();
  } else {
    return mrb_false_value();
  }
}

/*
 * GPIO.read_at(pin)
 */
static mrb_value
mrb_s_read_at(mrb_state *mrb, mrb_value klass)
{
  int pin_number = pin_num(mrb, NULL);
  gpio_claim(mrb, pin_number, GPIO_USE_READ);
  return mrb_fixnum_value(gpio_read_checked(mrb, pin_number));
}

/*
 * GPIO.write_at(pin, val)
 */
static mrb_value
mrb_s_write_at(mrb_state *mrb, mrb_value klass)
{
  int pin_number, value;
  pin_number = pin_num(mrb, &value);
  if (value != 0 && value != 1) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "Wrong value. 0 and 1 are only valid");
  }
  gpio_claim(mrb, pin_number, GPIO_USE_OUTPUT);
  GPIO_write(pin_number, value);
  return mrb_fixnum_value(0);
}

/*
 * GPIO.high?(pin)
 */
static mrb_value
mrb_high_p(mrb_state *mrb, mrb_value self)
{
  if (gpio_read_checked(mrb, (int)IVPINNUM()) == 0) {
    return mrb_false_value();
  } else {
    return mrb_true_value();
  }
}

/*
 * GPIO.low?(pin)
 */
static mrb_value
mrb_low_p(mrb_state *mrb, mrb_value self)
{
  if (gpio_read_checked(mrb, (int)IVPINNUM()) == 0) {
    return mrb_true_value();
  } else {
    return mrb_false_value();
  }
}

/*
 * GPIO.read(pin)
 */
static mrb_value
mrb_read(mrb_state *mrb, mrb_value self)
{
  return mrb_fixnum_value(gpio_read_checked(mrb, (int)IVPINNUM()));
}

/*
 * GPIO#write(val)
 */
static mrb_value
mrb_write(mrb_state *mrb, mrb_value self)
{
  mrb_int value;
  mrb_get_args(mrb, "i", &value);
  if (value != 0 && value != 1) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "Wrong value. 0 and 1 are only valid");
  }
  int pin_number = (int)IVPINNUM();
  gpio_claim(mrb, pin_number, GPIO_USE_OUTPUT);
  GPIO_write(pin_number, value);
  return mrb_fixnum_value(0);
}


void
mrb_picoruby_gpio_gem_init(mrb_state* mrb)
{
  struct RClass *class_GPIO = mrb_define_class_id(mrb, MRB_SYM(GPIO), mrb->object_class);

  mrb_define_const_id(mrb, class_GPIO, MRB_SYM(IN), mrb_fixnum_value(IN));
  mrb_define_const_id(mrb, class_GPIO, MRB_SYM(OUT), mrb_fixnum_value(OUT));
  mrb_define_const_id(mrb, class_GPIO, MRB_SYM(HIGH_Z), mrb_fixnum_value(HIGH_Z));
  mrb_define_const_id(mrb, class_GPIO, MRB_SYM(PULL_UP), mrb_fixnum_value(PULL_UP));
  mrb_define_const_id(mrb, class_GPIO, MRB_SYM(PULL_DOWN), mrb_fixnum_value(PULL_DOWN));
  mrb_define_const_id(mrb, class_GPIO, MRB_SYM(OPEN_DRAIN), mrb_fixnum_value(OPEN_DRAIN));
  mrb_define_const_id(mrb, class_GPIO, MRB_SYM(ALT), mrb_fixnum_value(ALT));

  mrb_define_method_id(mrb, class_GPIO, MRB_SYM(_init), mrb__init, MRB_ARGS_REQ(2));
  mrb_define_class_method_id(mrb, class_GPIO, MRB_SYM(set_function_at), mrb_s_set_function_at, MRB_ARGS_REQ(2));
  mrb_define_class_method_id(mrb, class_GPIO, MRB_SYM(set_dir_at), mrb_s_set_dir_at, MRB_ARGS_REQ(2));
  mrb_define_class_method_id(mrb, class_GPIO, MRB_SYM(pull_up_at), mrb_s_pull_up_at, MRB_ARGS_REQ(1));
  mrb_define_class_method_id(mrb, class_GPIO, MRB_SYM(pull_down_at), mrb_s_pull_down_at, MRB_ARGS_REQ(1));
  mrb_define_class_method_id(mrb, class_GPIO, MRB_SYM(open_drain_at), mrb_s_open_drain_at, MRB_ARGS_REQ(1));
  mrb_define_class_method_id(mrb, class_GPIO, MRB_SYM_Q(high_at), mrb_s_high_at_p, MRB_ARGS_REQ(1));
  mrb_define_class_method_id(mrb, class_GPIO, MRB_SYM_Q(low_at), mrb_s_low_at_p, MRB_ARGS_REQ(1));
  mrb_define_class_method_id(mrb, class_GPIO, MRB_SYM(read_at), mrb_s_read_at, MRB_ARGS_REQ(1));
  mrb_define_class_method_id(mrb, class_GPIO, MRB_SYM(write_at), mrb_s_write_at, MRB_ARGS_REQ(2));
  mrb_define_method_id(mrb, class_GPIO, MRB_SYM_Q(high), mrb_high_p, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, class_GPIO, MRB_SYM_Q(low), mrb_low_p, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, class_GPIO, MRB_SYM(read), mrb_read, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, class_GPIO, MRB_SYM(write), mrb_write, MRB_ARGS_REQ(1));
}

void
mrb_picoruby_gpio_gem_final(mrb_state* mrb)
{
}
