// Debug helper implementation
// Isolates mruby/proc.h and mruby/irep.h to avoid header conflicts

#include "app_debug.h"
#include "fmrb_log.h"

// Include proc/irep headers here (isolated from app.c)
#include <mruby/proc.h>
#include <mruby/irep.h>
#include <mruby/debug.h>
#include <mruby/opcode.h>
#include <mruby/value.h>
#include <mruby/string.h>

// mruby bytecode instruction format macros
// Based on mruby bytecode format (little-endian)
#ifndef GET_OPCODE
#define GET_OPCODE(i)     ((uint8_t)((i) & 0x7f))
#define GETARG_A(i)       ((uint16_t)(((i) >> 23) & 0x1ff))
#define GETARG_B(i)       ((uint16_t)(((i) >> 14) & 0x1ff))
#define GETARG_C(i)       ((uint8_t)(((i) >> 7) & 0x7f))
#endif

void app_debug_log_proc_details(
    mrb_state *mrb,
    const struct RProc *proc,
    const char *tag
)
{
    if (!proc) {
        FMRB_LOGW(tag, "ci->proc is NULL");
        return;
    }

    // Check if C function or Ruby method
    int is_cfunc = MRB_PROC_CFUNC_P(proc);
    FMRB_LOGI(tag, "ci->proc=%p flags=0x%x CFUNC=%d",
              proc, proc->flags, is_cfunc);

    if (is_cfunc) {
        // C function
        FMRB_LOGI(tag, "  C Function: func=%p", proc->body.func);
    } else {
        // Ruby method - log irep information
        const mrb_irep *irep = proc->body.irep;
        if (irep) {
            FMRB_LOGI(tag, "  Ruby Method: irep=%p", irep);
            FMRB_LOGI(tag, "    ilen=%u (bytecode instructions)", irep->ilen);
            FMRB_LOGI(tag, "    nlocals=%u nregs=%u", irep->nlocals, irep->nregs);

            // Try to get debug info (filename)
            if (irep->debug_info) {
                const char* filename = mrb_debug_get_filename(mrb, irep, 0);
                if (filename) {
                    FMRB_LOGI(tag, "    filename=%s", filename);
                } else {
                    FMRB_LOGI(tag, "    filename=(null)");
                }
            } else {
                FMRB_LOGI(tag, "    debug_info=NULL");
            }

            // Dump detailed bytecode
            app_debug_dump_irep_bytecode(mrb, irep, tag);
        } else {
            FMRB_LOGW(tag, "  Ruby Method: irep is NULL!");
        }
    }
}

// Helper: Get opcode name string
static const char* get_opcode_name(uint8_t opcode) {
    // mruby opcode names - simplified version
    static const char* opcode_names[] = {
        "NOP", "MOVE", "LOADL", "LOADI", "LOADINEG", "LOADI__1",
        "LOADI_0", "LOADI_1", "LOADI_2", "LOADI_3", "LOADI_4",
        "LOADI_5", "LOADI_6", "LOADI_7", "LOADSYM", "LOADNIL",
        "LOADSELF", "LOADT", "LOADF", "GETGV", "SETGV",
        "GETSV", "SETSV", "GETIV", "SETIV", "GETCV",
        "SETCV", "GETIDX", "SETIDX", "GETCONST", "SETCONST",
        "JMP", "JMPIF", "JMPNOT", "JMPNIL", "SENDV",
        "SENDVB", "SEND", "SENDB", "CALL", "SUPER",
        "ARGARY", "ENTER", "KEY_P", "KEYEND", "KARG",
        "RETURN", "RETURN_BLK", "BREAK", "BLKPUSH", "ADD",
        "ADDI", "SUB", "SUBI", "MUL", "DIV",
        "EQ", "LT", "LE", "GT", "GE",
        "ARRAY", "ARRAY2", "ARYCAT", "ARYPUSH", "ARYDUP",
        "AREF", "ASET", "APOST", "INTERN", "SYMBOL",
        "STRING", "STRCAT", "HASH", "HASHADD", "HASHCAT",
        "LAMBDA", "BLOCK", "METHOD", "RANGE_INC", "RANGE_EXC",
        "OCLASS", "CLASS", "MODULE", "EXEC", "DEF",
        "ALIAS", "UNDEF", "SCLASS", "TCLASS", "DEBUG",
        "ERR", "EXT1", "EXT2", "EXT3", "STOP"
    };

    if (opcode < sizeof(opcode_names) / sizeof(opcode_names[0])) {
        return opcode_names[opcode];
    }
    return "UNKNOWN";
}

void app_debug_dump_irep_bytecode(
    mrb_state *mrb,
    const void *irep_ptr,
    const char *tag
)
{
    const mrb_irep *irep = (const mrb_irep*)irep_ptr;
    if (!irep || !irep->iseq) {
        FMRB_LOGW(tag, "=== BYTECODE DUMP: irep or iseq is NULL ===");
        return;
    }

    FMRB_LOGI(tag, "=== BYTECODE DUMP START ===");
    FMRB_LOGI(tag, "irep=%p ilen=%u", irep, irep->ilen);

    // Dump bytecode instructions (limit to first 20 to avoid log overflow)
    uint32_t limit = irep->ilen < 20 ? irep->ilen : 20;
    for (uint32_t i = 0; i < limit; i++) {
        mrb_code code = irep->iseq[i];
        uint8_t opcode = GET_OPCODE(code);
        const char* op_name = get_opcode_name(opcode);

        // Decode instruction arguments
        uint16_t a = GETARG_A(code);
        uint16_t b = GETARG_B(code);
        uint8_t c = GETARG_C(code);

        FMRB_LOGI(tag, "  [%2u] %-12s A=%3u B=%3u C=%3u (0x%08x)",
                  i, op_name, a, b, c, code);
    }

    if (irep->ilen > 20) {
        FMRB_LOGI(tag, "  ... (%u more instructions omitted)", irep->ilen - 20);
    }

    // Dump symbol table
    if (irep->syms && irep->slen > 0) {
        FMRB_LOGI(tag, "Symbols (slen=%u):", irep->slen);
        uint16_t sym_limit = irep->slen < 10 ? irep->slen : 10;
        for (uint16_t i = 0; i < sym_limit; i++) {
            const char* sym_name = mrb_sym_name(mrb, irep->syms[i]);
            FMRB_LOGI(tag, "  [%2u] %s", i, sym_name ? sym_name : "(null)");
        }
        if (irep->slen > 10) {
            FMRB_LOGI(tag, "  ... (%u more symbols omitted)", irep->slen - 10);
        }
    }

    // Dump constant pool
    if (irep->pool && irep->plen > 0) {
        FMRB_LOGI(tag, "Pool (plen=%u):", irep->plen);
        uint16_t pool_limit = irep->plen < 10 ? irep->plen : 10;
        for (uint16_t i = 0; i < pool_limit; i++) {
            uint32_t tt = irep->pool[i].tt;
            uint32_t type = tt & 0x0F;  // Lower 4 bits

            if (type == IREP_TT_STR || type == IREP_TT_SSTR) {
                // String constant
                const char* str = irep->pool[i].u.str;
                FMRB_LOGI(tag, "  [%2u] String: \"%s\"", i, str ? str : "(null)");
            } else if (type == IREP_TT_INT32) {
                // 32-bit integer
                FMRB_LOGI(tag, "  [%2u] Int32: %d", i, irep->pool[i].u.i32);
            } else if (type == IREP_TT_INT64) {
                // 64-bit integer
                FMRB_LOGI(tag, "  [%2u] Int64: %lld", i, (long long)irep->pool[i].u.i64);
            } else if (type == IREP_TT_FLOAT) {
                // Float
                FMRB_LOGI(tag, "  [%2u] Float: %f", i, (double)irep->pool[i].u.f);
            } else {
                FMRB_LOGI(tag, "  [%2u] Type: %u", i, type);
            }
        }
        if (irep->plen > 10) {
            FMRB_LOGI(tag, "  ... (%u more pool entries omitted)", irep->plen - 10);
        }
    }

    FMRB_LOGI(tag, "=== BYTECODE DUMP END ===");
}

void app_debug_dump_callstack(
    mrb_state *mrb,
    const char *tag
)
{
    if (!mrb || !mrb->c) {
        FMRB_LOGE(tag, "=== CALLSTACK DUMP: mrb or mrb->c is NULL ===");
        return;
    }

    struct mrb_context *c = mrb->c;
    if (!c->cibase || !c->ci) {
        FMRB_LOGE(tag, "=== CALLSTACK DUMP: cibase or ci is NULL ===");
        return;
    }

    size_t ci_size = sizeof(mrb_callinfo);
    size_t depth = ((char*)c->ci - (char*)c->cibase) / ci_size;

    FMRB_LOGI(tag, "=== CALLSTACK DUMP START ===");
    FMRB_LOGI(tag, "Stack depth: %zu frames", depth + 1);
    FMRB_LOGI(tag, "cibase=%p ci=%p ciend=%p", c->cibase, c->ci, c->ciend);

    // Walk through call stack from cibase to ci
    for (size_t i = 0; i <= depth && i < 20; i++) {
        mrb_callinfo *frame = (mrb_callinfo*)((char*)c->cibase + i * ci_size);
        const struct RProc *proc = frame->proc;

        const char* marker = (frame == c->ci) ? " <- CURRENT" : "";

        if (proc) {
            int is_cfunc = MRB_PROC_CFUNC_P(proc);
            if (is_cfunc) {
                FMRB_LOGI(tag, "[%2zu] cibase+%3zu proc=%p (C function: %p)%s",
                          i, i * ci_size, proc, proc->body.func, marker);
            } else {
                const mrb_irep *irep = proc->body.irep;
                const char* filename = "(unknown)";
                if (irep && irep->debug_info) {
                    const char* f = mrb_debug_get_filename(mrb, irep, 0);
                    if (f) filename = f;
                }
                FMRB_LOGI(tag, "[%2zu] cibase+%3zu proc=%p (Ruby: %s)%s",
                          i, i * ci_size, proc, filename, marker);
            }
        } else {
            FMRB_LOGI(tag, "[%2zu] cibase+%3zu proc=NULL%s",
                      i, i * ci_size, marker);
        }
    }

    if (depth > 20) {
        FMRB_LOGI(tag, "... (%zu more frames omitted)", depth - 20);
    }

    FMRB_LOGI(tag, "=== CALLSTACK DUMP END ===");
}
