#include <string.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/hash.h>
#include <mruby/array.h>
#include "fmrb_bmp332.h"

// BMP332.parse(data_string) -> { width:, height:, pixels: String }
// Parses raw BMP file data and returns pixel array as RGB332 bytes (top-down)
static mrb_value mrb_bmp332_parse(mrb_state *mrb, mrb_value self)
{
    mrb_value data_str;
    mrb_get_args(mrb, "S", &data_str);

    mrb_int len = RSTRING_LEN(data_str);
    if (len < 54) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "BMP data too small");
    }

    // Make a mutable copy (parser modifies buffer for row flipping)
    uint8_t *buf = (uint8_t *)mrb_malloc(mrb, len);
    memcpy(buf, RSTRING_PTR(data_str), len);

    fmrb_bmp332_t bmp;
    int ret = fmrb_bmp332_parse(buf, (size_t)len, &bmp);

    if (ret != 0) {
        mrb_free(mrb, buf);
        mrb_raise(mrb, E_RUNTIME_ERROR, "Failed to parse BMP332");
    }

    // Build result hash
    mrb_value result = mrb_hash_new(mrb);
    mrb_hash_set(mrb, result, mrb_symbol_value(mrb_intern_lit(mrb, "width")),
                 mrb_fixnum_value(bmp.width));
    mrb_hash_set(mrb, result, mrb_symbol_value(mrb_intern_lit(mrb, "height")),
                 mrb_fixnum_value(bmp.height));
    mrb_hash_set(mrb, result, mrb_symbol_value(mrb_intern_lit(mrb, "pixels")),
                 mrb_str_new(mrb, (const char *)bmp.pixels, bmp.pixels_size));

    mrb_free(mrb, buf);
    return result;
}

void mrb_picoruby_fmrb_bmp332_gem_init(mrb_state *mrb)
{
    struct RClass *bmp_module = mrb_define_module(mrb, "BMP332");
    mrb_define_module_function(mrb, bmp_module, "parse", mrb_bmp332_parse, MRB_ARGS_REQ(1));
}

void mrb_picoruby_fmrb_bmp332_gem_final(mrb_state *mrb)
{
}
