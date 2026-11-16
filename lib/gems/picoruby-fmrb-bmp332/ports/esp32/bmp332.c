#include <string.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/hash.h>
#include <mruby/array.h>
#include "fmrb_bmp332.h"
#include "fmrb_png332.h"

// The PNG code's buffers come from the app's VM pool. The non-raising
// allocator, so nothing leaks past a raise in the middle of a decode.
static void *png_alloc(void *ud, size_t size)
{
    return mrb_malloc_simple((mrb_state *)ud, size);
}

static void png_free(void *ud, void *p)
{
    mrb_free((mrb_state *)ud, p);
}

static mrb_value result_hash(mrb_state *mrb, int width, int height, const uint8_t *pixels, size_t size)
{
    mrb_value result = mrb_hash_new(mrb);
    mrb_hash_set(mrb, result, mrb_symbol_value(mrb_intern_lit(mrb, "width")),
                 mrb_fixnum_value(width));
    mrb_hash_set(mrb, result, mrb_symbol_value(mrb_intern_lit(mrb, "height")),
                 mrb_fixnum_value(height));
    mrb_hash_set(mrb, result, mrb_symbol_value(mrb_intern_lit(mrb, "pixels")),
                 mrb_str_new(mrb, (const char *)pixels, size));
    return result;
}

static mrb_value parse_png(mrb_state *mrb, mrb_value data_str)
{
    fmrb_png332_allocator_t a = { png_alloc, png_free, mrb };
    uint16_t w = 0, h = 0;
    uint8_t *px = NULL;
    int ret = fmrb_png332_decode((const uint8_t *)RSTRING_PTR(data_str),
                                 (size_t)RSTRING_LEN(data_str), &a, &w, &h, &px);
    if (ret != FMRB_PNG332_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to read PNG: %s", fmrb_png332_error(ret));
    }
    // The pixels go into a string before the hash is made, so only that one
    // allocation can raise while the buffer is held (the VM pool goes with
    // the app in any case).
    mrb_value pixels = mrb_str_new(mrb, (const char *)px, (size_t)w * h);
    mrb_free(mrb, px);
    mrb_value result = mrb_hash_new(mrb);
    mrb_hash_set(mrb, result, mrb_symbol_value(mrb_intern_lit(mrb, "width")),
                 mrb_fixnum_value(w));
    mrb_hash_set(mrb, result, mrb_symbol_value(mrb_intern_lit(mrb, "height")),
                 mrb_fixnum_value(h));
    mrb_hash_set(mrb, result, mrb_symbol_value(mrb_intern_lit(mrb, "pixels")), pixels);
    return result;
}

// BMP332.parse(data_string) -> { width:, height:, pixels: String }
// Parses a picture file's bytes -- an 8-bit BMP or a PNG (told apart by the
// first bytes) -- into RGB332 pixel bytes, top-down.
static mrb_value mrb_bmp332_parse(mrb_state *mrb, mrb_value self)
{
    mrb_value data_str;
    mrb_get_args(mrb, "S", &data_str);

    mrb_int len = RSTRING_LEN(data_str);
    if (fmrb_png332_is_png((const uint8_t *)RSTRING_PTR(data_str), (size_t)len)) {
        return parse_png(mrb, data_str);
    }
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

    mrb_value result = result_hash(mrb, bmp.width, bmp.height, bmp.pixels, bmp.pixels_size);
    mrb_free(mrb, buf);
    return result;
}

// BMP332.encode_png(width, height, pixels) -> String
// The palette PNG of tools/rgb332_png.rb for RGB332 pixel bytes (top-down).
static mrb_value mrb_bmp332_encode_png(mrb_state *mrb, mrb_value self)
{
    mrb_int w, h;
    mrb_value px_str;
    mrb_get_args(mrb, "iiS", &w, &h, &px_str);
    if (w <= 0 || h <= 0 || w > FMRB_PNG332_MAX_DIM || h > FMRB_PNG332_MAX_DIM) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "PNG size out of range");
    }
    if (RSTRING_LEN(px_str) != w * h) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "pixels must be width * height bytes");
    }
    fmrb_png332_allocator_t a = { png_alloc, png_free, mrb };
    uint8_t *png = NULL;
    size_t png_len = 0;
    int ret = fmrb_png332_encode((const uint8_t *)RSTRING_PTR(px_str), (uint16_t)w, (uint16_t)h,
                                 &a, &png, &png_len);
    if (ret != FMRB_PNG332_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to write PNG: %s", fmrb_png332_error(ret));
    }
    mrb_value out = mrb_str_new(mrb, (const char *)png, png_len);
    mrb_free(mrb, png);
    return out;
}

void mrb_picoruby_fmrb_bmp332_gem_init(mrb_state *mrb)
{
    struct RClass *bmp_module = mrb_define_module(mrb, "BMP332");
    mrb_define_module_function(mrb, bmp_module, "parse", mrb_bmp332_parse, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, bmp_module, "encode_png", mrb_bmp332_encode_png, MRB_ARGS_REQ(3));
}

void mrb_picoruby_fmrb_bmp332_gem_final(mrb_state *mrb)
{
}
