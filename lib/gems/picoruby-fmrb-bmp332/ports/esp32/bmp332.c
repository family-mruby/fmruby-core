#include <string.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/hash.h>
#include <mruby/array.h>
#include "fmrb_bmp332.h"
#include "fmrb_png332.h"
#include "fmrb_picture332.h"

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

static void hash_set(mrb_state *mrb, mrb_value h, mrb_sym key, mrb_value v)
{
    mrb_hash_set(mrb, h, mrb_symbol_value(key), v);
}

// The picture source of info / to_png: the positional path, or data: (a
// String of the file's bytes). Raises when neither or both are given.
static void picture_source(mrb_state *mrb, mrb_value path, mrb_value data)
{
    int has_path = !mrb_nil_p(path), has_data = !mrb_undef_p(data) && !mrb_nil_p(data);
    if (has_path == has_data) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "give a path or data:, one of them");
    }
    if (has_path && !mrb_string_p(path)) mrb_raise(mrb, E_TYPE_ERROR, "path must be a String");
    if (has_data && !mrb_string_p(data)) mrb_raise(mrb, E_TYPE_ERROR, "data: must be a String");
}

// BMP332.info(path) / BMP332.info(data: bytes)
//   -> { width:, height:, format: :png | :bmp, bytes: } or nil
// Only the head is looked at (a PNG's IHDR, a BMP's headers); nothing is
// decoded. nil when it is not there or not a picture BMP332 reads. The size
// is the picture's own, even past what BMP332 decodes.
static mrb_value mrb_bmp332_info(mrb_state *mrb, mrb_value self)
{
    mrb_value path = mrb_nil_value();
    const mrb_sym kw_names[] = { mrb_intern_lit(mrb, "data") };
    mrb_value kw_values[1];
    mrb_kwargs kwargs = { 1, 0, kw_names, kw_values, NULL };
    mrb_get_args(mrb, "|o:", &path, &kwargs);
    picture_source(mrb, path, kw_values[0]);

    fmrb_picture332_file_info_t info;
    if (!mrb_nil_p(path)) {
        if (fmrb_picture332_file_info(mrb_str_to_cstr(mrb, path), &info) != 0) return mrb_nil_value();
    } else {
        fmrb_picture332_head_t head;
        mrb_value data = kw_values[0];
        if (fmrb_picture332_head((const uint8_t *)RSTRING_PTR(data), (size_t)RSTRING_LEN(data), &head) != 0) {
            return mrb_nil_value();
        }
        info.width = head.width;
        info.height = head.height;
        info.format = head.format;
        info.bytes = (uint32_t)RSTRING_LEN(data);
    }
    mrb_value h = mrb_hash_new_capa(mrb, 4);
    hash_set(mrb, h, mrb_intern_lit(mrb, "width"), mrb_fixnum_value(info.width));
    hash_set(mrb, h, mrb_intern_lit(mrb, "height"), mrb_fixnum_value(info.height));
    hash_set(mrb, h, mrb_intern_lit(mrb, "format"),
             mrb_symbol_value(info.format == FMRB_PICTURE332_PNG ? mrb_intern_lit(mrb, "png")
                                                                 : mrb_intern_lit(mrb, "bmp")));
    hash_set(mrb, h, mrb_intern_lit(mrb, "bytes"), mrb_int_value(mrb, (mrb_int)info.bytes));
    return h;
}

static mrb_value refusal(mrb_state *mrb, const char *why)
{
    mrb_value h = mrb_hash_new_capa(mrb, 3);
    hash_set(mrb, h, mrb_intern_lit(mrb, "error"), mrb_symbol_value(mrb_intern_cstr(mrb, why)));
    return h;
}

// An integer keyword, or its default when it is not given.
static mrb_int kw_int(mrb_state *mrb, mrb_value v, mrb_int def, mrb_int min)
{
    if (mrb_undef_p(v) || mrb_nil_p(v)) return def;
    mrb_int n = mrb_as_int(mrb, v);
    return n < min ? min : n;
}

// BMP332.to_png(path, max_width: 2048, max_height: 2048, max_bytes: nil,
//               max_file_bytes: 65536) / BMP332.to_png(data: bytes, ...)
//   -> { png: String, width:, height: }
//    | { error: :too_big, width:, height: } | { error: :too_heavy, bytes: }
//    | { error: :file_too_large, bytes: } | { error: :unreadable }
//    | { error: :no_memory }
// A PNG or BMP read and written again as the palette PNG, all here: the
// bytes are those of BMP332.encode_png over BMP332.parse's pixels, and the
// VM keeps only the PNG and the Hash. The file (or data:) larger than
// max_file_bytes is not read; a side past max_width / max_height is refused
// from the head, before anything is decoded; a PNG past max_bytes after it
// is written. Nothing raises for a picture (only for wrong arguments).
static mrb_value mrb_bmp332_to_png(mrb_state *mrb, mrb_value self)
{
    mrb_value path = mrb_nil_value();
    const mrb_sym kw_names[] = {
        mrb_intern_lit(mrb, "data"), mrb_intern_lit(mrb, "max_width"), mrb_intern_lit(mrb, "max_height"),
        mrb_intern_lit(mrb, "max_bytes"), mrb_intern_lit(mrb, "max_file_bytes"),
    };
    mrb_value kw_values[5];
    mrb_kwargs kwargs = { 5, 0, kw_names, kw_values, NULL };
    mrb_get_args(mrb, "|o:", &path, &kwargs);
    picture_source(mrb, path, kw_values[0]);
    mrb_int max_w = kw_int(mrb, kw_values[1], FMRB_PNG332_MAX_DIM, 0);
    mrb_int max_h = kw_int(mrb, kw_values[2], FMRB_PNG332_MAX_DIM, 0);
    mrb_int max_bytes = kw_int(mrb, kw_values[3], 0, 0);
    mrb_int max_file = kw_int(mrb, kw_values[4], 65536, 0);
    if (max_w > 0xFFFF) max_w = 0xFFFF;
    if (max_h > 0xFFFF) max_h = 0xFFFF;

    fmrb_png332_allocator_t a = { png_alloc, png_free, mrb };
    const uint8_t *src;
    size_t src_len;
    uint8_t *file = NULL;
    if (!mrb_nil_p(path)) {
        int r = fmrb_picture332_file_read(mrb_str_to_cstr(mrb, path), (size_t)max_file, &a, &file, &src_len);
        if (r == FMRB_PICTURE332_E_FILE_TOO_LARGE) {
            mrb_value h = refusal(mrb, "file_too_large");
            hash_set(mrb, h, mrb_intern_lit(mrb, "bytes"), mrb_int_value(mrb, (mrb_int)src_len));
            return h;
        }
        if (r == FMRB_PICTURE332_E_NOMEM) return refusal(mrb, "no_memory");
        if (r != FMRB_PICTURE332_OK) return refusal(mrb, "unreadable");
        src = file;
    } else {
        mrb_value data = kw_values[0];
        src = (const uint8_t *)RSTRING_PTR(data);
        src_len = (size_t)RSTRING_LEN(data);
        if (src_len > (size_t)max_file) {
            mrb_value h = refusal(mrb, "file_too_large");
            hash_set(mrb, h, mrb_intern_lit(mrb, "bytes"), mrb_int_value(mrb, (mrb_int)src_len));
            return h;
        }
    }

    uint8_t *png = NULL;
    size_t png_len = 0;
    uint16_t w = 0, h = 0;
    int r = fmrb_picture332_to_png(src, src_len, (uint16_t)max_w, (uint16_t)max_h, (size_t)max_bytes, &a,
                                   &png, &png_len, &w, &h);
    // The file's buffer goes before anything of the VM is made (a raise
    // past here cannot leave it held).
    if (file) mrb_free(mrb, file);
    if (r == FMRB_PICTURE332_E_TOO_BIG) {
        mrb_value res = refusal(mrb, "too_big");
        hash_set(mrb, res, mrb_intern_lit(mrb, "width"), mrb_fixnum_value(w));
        hash_set(mrb, res, mrb_intern_lit(mrb, "height"), mrb_fixnum_value(h));
        return res;
    }
    if (r == FMRB_PICTURE332_E_TOO_HEAVY) {
        mrb_value res = refusal(mrb, "too_heavy");
        hash_set(mrb, res, mrb_intern_lit(mrb, "bytes"), mrb_int_value(mrb, (mrb_int)png_len));
        return res;
    }
    if (r == FMRB_PICTURE332_E_NOMEM) return refusal(mrb, "no_memory");
    if (r != FMRB_PICTURE332_OK) return refusal(mrb, "unreadable");
    mrb_value s = mrb_str_new(mrb, (const char *)png, png_len);
    mrb_free(mrb, png);
    mrb_value res = mrb_hash_new_capa(mrb, 3);
    hash_set(mrb, res, mrb_intern_lit(mrb, "png"), s);
    hash_set(mrb, res, mrb_intern_lit(mrb, "width"), mrb_fixnum_value(w));
    hash_set(mrb, res, mrb_intern_lit(mrb, "height"), mrb_fixnum_value(h));
    return res;
}

void mrb_picoruby_fmrb_bmp332_gem_init(mrb_state *mrb)
{
    struct RClass *bmp_module = mrb_define_module(mrb, "BMP332");
    mrb_define_module_function(mrb, bmp_module, "parse", mrb_bmp332_parse, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, bmp_module, "encode_png", mrb_bmp332_encode_png, MRB_ARGS_REQ(3));
    mrb_define_module_function(mrb, bmp_module, "info", mrb_bmp332_info, MRB_ARGS_OPT(1) | MRB_ARGS_KEY(1, 0));
    mrb_define_module_function(mrb, bmp_module, "to_png", mrb_bmp332_to_png, MRB_ARGS_OPT(1) | MRB_ARGS_KEY(5, 0));
}

void mrb_picoruby_fmrb_bmp332_gem_final(mrb_state *mrb)
{
}
