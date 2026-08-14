/*
 * RaycastNative -- the thin mruby side of the raycast gem (mrblib/raycast.rb).
 *
 * Nothing is computed here. The map arrives as a byte String and is handed to
 * the receiver; a cast passes three integers in and returns
 * [microseconds, packed depth buffer]. The Ruby unpacks that into the Hashes
 * the game's drawing code already reads.
 *
 * It lives in ports/esp32 (compiled by the picoruby-esp32 component) because it
 * needs main/'s headers, which the rake mruby build does not have on its
 * include path -- the same split the other fmrb gems use.
 */
#include <stdint.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/string.h>

#include "raycast_native.h"

static mrb_value mrb_rc_available(mrb_state *mrb, mrb_value self)
{
    (void)self; (void)mrb;
    return mrb_bool_value(raycast_available() != 0);
}

static mrb_value mrb_rc_micros(mrb_state *mrb, mrb_value self)
{
    (void)self; (void)mrb;
    return mrb_fixnum_value((mrb_int)raycast_micros());
}

static mrb_value mrb_rc_begin(mrb_state *mrb, mrb_value self)
{
    (void)self; (void)mrb;
    return mrb_fixnum_value(raycast_begin());
}

static mrb_value mrb_rc_set_map(mrb_state *mrb, mrb_value self)
{
    (void)self;
    mrb_value cells;
    mrb_int w, h;
    mrb_get_args(mrb, "Sii", &cells, &w, &h);
    if (w < 1 || h < 1) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "map dimensions must be positive");
    }
    if (RSTRING_LEN(cells) < w * h) {
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "need %d map cells, got %d bytes",
                   (int)(w * h), (int)RSTRING_LEN(cells));
    }
    return mrb_fixnum_value(raycast_set_map((const uint8_t *)RSTRING_PTR(cells),
                                            (int)w, (int)h));
}

static mrb_value mrb_rc_cast(mrb_state *mrb, mrb_value self)
{
    (void)self;
    mrb_int px, py, pa;
    mrb_get_args(mrb, "iii", &px, &py, &pa);

    int len = 0;
    uint32_t us = 0;
    const char *buf = raycast_run((int)px, (int)py, (int)pa, &len, &us);

    mrb_value out = mrb_ary_new_capa(mrb, 2);
    mrb_ary_push(mrb, out, mrb_fixnum_value((mrb_int)us));
    if (!buf || len <= 0) {
        mrb_ary_push(mrb, out, mrb_str_new(mrb, "", 0));
    } else {
        mrb_ary_push(mrb, out, mrb_str_new(mrb, buf, (size_t)len));
    }
    return out;
}

static mrb_value mrb_rc_end(mrb_state *mrb, mrb_value self)
{
    (void)self; (void)mrb;
    raycast_end();
    return mrb_nil_value();
}

void mrb_fmrb_raycast_init(mrb_state *mrb)
{
    struct RClass *m = mrb_define_module(mrb, "RaycastNative");
    mrb_define_module_function(mrb, m, "available?", mrb_rc_available, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, m, "micros", mrb_rc_micros, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, m, "begin_instance", mrb_rc_begin, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, m, "set_map", mrb_rc_set_map, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, m, "cast", mrb_rc_cast, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, m, "end_instance", mrb_rc_end, MRB_ARGS_NONE());
}

void mrb_fmrb_raycast_final(mrb_state *mrb)
{
    (void)mrb;
    raycast_end();
}
