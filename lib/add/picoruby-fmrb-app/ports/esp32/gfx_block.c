#include <string.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/string.h>
#include <mruby/variable.h>
#include <mruby/array.h>

#include "fmrb_gfx.h"
#include "fmrb_log.h"
#include "fmrb_link_protocol.h"
#include "fmrb_mem.h"

static const char *TAG = "gfx_block";

// Opcodes must match gfx_vm.h on the WROVER side.
#define OP_END             0x00
#define OP_CLEAR           0x01
#define OP_FILL_RECT       0x02
#define OP_DRAW_RECT       0x03
#define OP_FILL_ROUND_RECT 0x04
#define OP_DRAW_ROUND_RECT 0x05
#define OP_DRAW_LINE       0x06
#define OP_FILL_CIRCLE     0x07
#define OP_DRAW_TEXT       0x08

// Access the canvas_id/ctx stored on the FmrbGfx instance. Mirrors mrb_gfx_data
// in gfx.c; duplicated here because the struct is file-scope there.
typedef struct {
    fmrb_gfx_context_t ctx;
    fmrb_canvas_handle_t canvas_id;
} gfx_data_view_t;

// Extract (ctx, canvas_id) from a FmrbGfx instance value.
static bool get_gfx(mrb_state *mrb, mrb_value gfx_self,
                    fmrb_gfx_context_t *out_ctx,
                    fmrb_canvas_handle_t *out_canvas_id)
{
    if (mrb_type(gfx_self) != MRB_TT_DATA) return false;
    gfx_data_view_t *d = (gfx_data_view_t *)DATA_PTR(gfx_self);
    if (!d || !d->ctx) return false;
    *out_ctx = d->ctx;
    *out_canvas_id = d->canvas_id;
    return true;
}

// Encode an operand into 2 bytes (little-endian).
//   is_reg=true  -> bit15=1, low 4 bits = reg_id
//   is_reg=false -> bit15=0, 15-bit signed immediate
// Clips out-of-range immediates to [-16384, 16383].
static void emit_operand(uint8_t *buf, bool is_reg, int val)
{
    uint16_t w;
    if (is_reg) {
        w = 0x8000u | (uint16_t)(val & 0x0F);
    } else {
        if (val > 16383) val = 16383;
        if (val < -16384) val = -16384;
        w = (uint16_t)(val & 0x7FFF);
    }
    buf[0] = (uint8_t)(w & 0xFF);
    buf[1] = (uint8_t)((w >> 8) & 0xFF);
}

// _gfx_compile_block(cmds, var_map, strings) -> [bytecode_str, strtable_str]
//   cmds:    Array of Array<Integer>. cmds[i][0]=opcode, cmds[i][1..]=args
//   var_map: Array of Array parallel to cmds. var_map[i][j+1] corresponds to
//            cmds[i][j+1]: nil/false => immediate, Integer => reg_id
//   strings: Array of String. Packed as [len(1)][chars(len)]...
static mrb_value mrb_gfx_compile_block(mrb_state *mrb, mrb_value self)
{
    (void)self;
    mrb_value cmds, var_map, strings;
    mrb_get_args(mrb, "AAA", &cmds, &var_map, &strings);

    mrb_int n_cmds = RARRAY_LEN(cmds);
    if (n_cmds != RARRAY_LEN(var_map)) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "cmds/var_map length mismatch");
    }

    // Worst-case bytecode size: each command = 2 (op+nargs) + nargs*2, plus END byte.
    // We allocate a temporary buffer and copy to a real mrb_str once done.
    uint8_t tmp[512];
    size_t pos = 0;

    for (mrb_int i = 0; i < n_cmds; i++) {
        mrb_value cmd = mrb_ary_ref(mrb, cmds, i);
        mrb_value vmap = mrb_ary_ref(mrb, var_map, i);
        if (!mrb_array_p(cmd) || !mrb_array_p(vmap)) {
            mrb_raise(mrb, E_ARGUMENT_ERROR, "cmd/vmap must be Array");
        }
        mrb_int len = RARRAY_LEN(cmd);
        if (len != RARRAY_LEN(vmap)) {
            mrb_raise(mrb, E_ARGUMENT_ERROR, "cmd/vmap inner length mismatch");
        }
        if (len < 1) {
            mrb_raise(mrb, E_ARGUMENT_ERROR, "empty command");
        }
        mrb_int nargs = len - 1;
        if (nargs > 8) {
            mrb_raise(mrb, E_ARGUMENT_ERROR, "too many args (max 8)");
        }
        if (pos + 2 + nargs * 2 > sizeof(tmp) - 1) {
            mrb_raise(mrb, E_RUNTIME_ERROR, "bytecode too large");
        }

        mrb_value opcode_v = mrb_ary_ref(mrb, cmd, 0);
        tmp[pos++] = (uint8_t)mrb_fixnum(opcode_v);
        tmp[pos++] = (uint8_t)nargs;

        for (mrb_int j = 0; j < nargs; j++) {
            mrb_value arg_v = mrb_ary_ref(mrb, cmd, j + 1);
            mrb_value vf    = mrb_ary_ref(mrb, vmap, j + 1);
            int int_val = 0;
            bool is_reg = mrb_fixnum_p(vf);
            if (is_reg) {
                int_val = (int)mrb_fixnum(vf);
                if (int_val < 0 || int_val > 15) {
                    mrb_raise(mrb, E_ARGUMENT_ERROR, "reg_id out of range");
                }
            } else {
                if (!mrb_fixnum_p(arg_v)) {
                    mrb_raisef(mrb, E_TYPE_ERROR,
                               "non-integer immediate at cmd %d arg %d",
                               (int)i, (int)j);
                }
                int_val = (int)mrb_fixnum(arg_v);
            }
            emit_operand(&tmp[pos], is_reg, int_val);
            pos += 2;
        }
    }
    tmp[pos++] = OP_END;

    mrb_value bc_str = mrb_str_new(mrb, (const char *)tmp, pos);

    // Build strtable: [len(1)][chars(len)]...
    uint8_t stbuf[128];
    size_t st_pos = 0;
    mrb_int n_strings = RARRAY_LEN(strings);
    for (mrb_int i = 0; i < n_strings; i++) {
        mrb_value s = mrb_ary_ref(mrb, strings, i);
        if (!mrb_string_p(s)) mrb_raise(mrb, E_TYPE_ERROR, "string expected");
        mrb_int slen = RSTRING_LEN(s);
        if (slen > 255) mrb_raise(mrb, E_ARGUMENT_ERROR, "string too long (>255)");
        if (st_pos + 1 + (size_t)slen > sizeof(stbuf)) {
            mrb_raise(mrb, E_RUNTIME_ERROR, "strtable too large");
        }
        stbuf[st_pos++] = (uint8_t)slen;
        memcpy(&stbuf[st_pos], RSTRING_PTR(s), (size_t)slen);
        st_pos += (size_t)slen;
    }
    mrb_value st_str = mrb_str_new(mrb, (const char *)stbuf, st_pos);

    mrb_value result = mrb_ary_new_capa(mrb, 2);
    mrb_ary_push(mrb, result, bc_str);
    mrb_ary_push(mrb, result, st_str);
    return result;
}

// _gfx_define_prog(bytecode_str, strtable_str) -> Integer prog_id
static mrb_value mrb_gfx_define_prog(mrb_state *mrb, mrb_value self)
{
    mrb_value bc_str, st_str;
    mrb_get_args(mrb, "SS", &bc_str, &st_str);

    fmrb_gfx_context_t ctx;
    fmrb_canvas_handle_t canvas_id;
    if (!get_gfx(mrb, self, &ctx, &canvas_id)) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "FmrbGfx not initialized");
    }

    uint8_t prog_id = FMRB_GFX_VM_INVALID_PROG_ID;
    fmrb_gfx_err_t ret = fmrb_gfx_define_prog(
        ctx, canvas_id,
        (const uint8_t *)RSTRING_PTR(bc_str), (uint16_t)RSTRING_LEN(bc_str),
        (const uint8_t *)RSTRING_PTR(st_str), (uint16_t)RSTRING_LEN(st_str),
        &prog_id);
    if (ret != FMRB_GFX_OK) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "define_prog failed: %d", (int)ret);
    }
    return mrb_fixnum_value(prog_id);
}

// _gfx_exec_prog(prog_id, reg_updates)
//   reg_updates: Array of [reg_id, value] pairs (each a 2-elem Array)
static mrb_value mrb_gfx_exec_prog(mrb_state *mrb, mrb_value self)
{
    mrb_int prog_id_i;
    mrb_value reg_updates;
    mrb_get_args(mrb, "iA", &prog_id_i, &reg_updates);

    fmrb_gfx_context_t ctx;
    fmrb_canvas_handle_t canvas_id;
    if (!get_gfx(mrb, self, &ctx, &canvas_id)) {
        mrb_raise(mrb, E_RUNTIME_ERROR, "FmrbGfx not initialized");
    }

    mrb_int n = RARRAY_LEN(reg_updates);
    if (n > 16) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "too many reg updates (max 16)");
    }
    uint8_t buf[3 * 16];
    for (mrb_int i = 0; i < n; i++) {
        mrb_value pair = mrb_ary_ref(mrb, reg_updates, i);
        if (!mrb_array_p(pair) || RARRAY_LEN(pair) != 2) {
            mrb_raise(mrb, E_ARGUMENT_ERROR, "each reg update must be [id, value]");
        }
        mrb_int reg_id = mrb_fixnum(mrb_ary_ref(mrb, pair, 0));
        mrb_int val    = mrb_fixnum(mrb_ary_ref(mrb, pair, 1));
        if (reg_id < 0 || reg_id > 15) {
            mrb_raise(mrb, E_ARGUMENT_ERROR, "reg_id out of range");
        }
        int16_t v16 = (int16_t)val;
        buf[i * 3 + 0] = (uint8_t)reg_id;
        buf[i * 3 + 1] = (uint8_t)(v16 & 0xFF);
        buf[i * 3 + 2] = (uint8_t)((v16 >> 8) & 0xFF);
    }

    fmrb_gfx_err_t ret = fmrb_gfx_exec_prog(ctx, canvas_id,
                                             (uint8_t)prog_id_i,
                                             buf, (uint8_t)n);
    if (ret != FMRB_GFX_OK) {
        FMRB_LOGW(TAG, "exec_prog failed: %d", (int)ret);
    }
    return self;
}

// _gfx_delete_prog(prog_id)
static mrb_value mrb_gfx_delete_prog(mrb_state *mrb, mrb_value self)
{
    mrb_int prog_id_i;
    mrb_get_args(mrb, "i", &prog_id_i);

    fmrb_gfx_context_t ctx;
    fmrb_canvas_handle_t canvas_id;
    (void)canvas_id;
    if (!get_gfx(mrb, self, &ctx, &canvas_id)) {
        return self;  // Silent no-op if FmrbGfx already destroyed
    }

    fmrb_gfx_delete_prog(ctx, (uint8_t)prog_id_i);
    return self;
}

void mrb_fmrb_gfx_block_init(mrb_state *mrb)
{
    struct RClass *gfx_class = mrb_class_get(mrb, "FmrbGfx");
    mrb_define_method(mrb, gfx_class, "_gfx_compile_block",
                      mrb_gfx_compile_block, MRB_ARGS_REQ(3));
    mrb_define_method(mrb, gfx_class, "_gfx_define_prog",
                      mrb_gfx_define_prog, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, gfx_class, "_gfx_exec_prog",
                      mrb_gfx_exec_prog, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, gfx_class, "_gfx_delete_prog",
                      mrb_gfx_delete_prog, MRB_ARGS_REQ(1));

    // Opcode constants (for Ruby-side DSL to use)
    mrb_define_const(mrb, gfx_class, "GFXVM_OP_CLEAR",           mrb_fixnum_value(OP_CLEAR));
    mrb_define_const(mrb, gfx_class, "GFXVM_OP_FILL_RECT",       mrb_fixnum_value(OP_FILL_RECT));
    mrb_define_const(mrb, gfx_class, "GFXVM_OP_DRAW_RECT",       mrb_fixnum_value(OP_DRAW_RECT));
    mrb_define_const(mrb, gfx_class, "GFXVM_OP_FILL_ROUND_RECT", mrb_fixnum_value(OP_FILL_ROUND_RECT));
    mrb_define_const(mrb, gfx_class, "GFXVM_OP_DRAW_ROUND_RECT", mrb_fixnum_value(OP_DRAW_ROUND_RECT));
    mrb_define_const(mrb, gfx_class, "GFXVM_OP_DRAW_LINE",       mrb_fixnum_value(OP_DRAW_LINE));
    mrb_define_const(mrb, gfx_class, "GFXVM_OP_FILL_CIRCLE",     mrb_fixnum_value(OP_FILL_CIRCLE));
    mrb_define_const(mrb, gfx_class, "GFXVM_OP_DRAW_TEXT",       mrb_fixnum_value(OP_DRAW_TEXT));
}
