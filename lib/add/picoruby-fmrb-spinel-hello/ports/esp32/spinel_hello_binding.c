/*
 * SpinelHelloNative -- the thin mruby side of the minimal Spinel sample
 * (mrblib/spinel_hello.rb). It only forwards to the receiver in
 * native/spinel_hello_native.c, which runs the Spinel-compiled entry and hands
 * back the greeting string.
 *
 * It lives in ports/esp32 (compiled by the picoruby-esp32 component) because it
 * needs main/'s headers, which the rake mruby build does not have on its
 * include path -- the same split the other fmrb gems use.
 */
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/string.h>

#include "spinel_hello_native.h"

static mrb_value mrb_sh_available(mrb_state *mrb, mrb_value self)
{
    (void)self; (void)mrb;
    return mrb_bool_value(spinel_hello_available() != 0);
}

static mrb_value mrb_sh_begin(mrb_state *mrb, mrb_value self)
{
    (void)self; (void)mrb;
    return mrb_fixnum_value(spinel_hello_begin());
}

static mrb_value mrb_sh_greet(mrb_state *mrb, mrb_value self)
{
    (void)self;
    int len = 0;
    const char *s = spinel_hello_run(&len);
    if (!s || len <= 0) {
        return mrb_str_new(mrb, "", 0);
    }
    return mrb_str_new(mrb, s, (size_t)len);
}

static mrb_value mrb_sh_end(mrb_state *mrb, mrb_value self)
{
    (void)self; (void)mrb;
    spinel_hello_end();
    return mrb_nil_value();
}

void mrb_fmrb_spinel_hello_init(mrb_state *mrb)
{
    struct RClass *m = mrb_define_module(mrb, "SpinelHelloNative");
    mrb_define_module_function(mrb, m, "available?", mrb_sh_available, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, m, "begin_instance", mrb_sh_begin, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, m, "greet", mrb_sh_greet, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, m, "end_instance", mrb_sh_end, MRB_ARGS_NONE());
}

void mrb_fmrb_spinel_hello_final(mrb_state *mrb)
{
    (void)mrb;
    spinel_hello_end();
}
