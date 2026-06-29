// GfxBlock bytecode VM for the ESP32-P4 / Tab5 display task.
// Mirrors the gfx_vm interface (fmruby-graphics-audio) but is a local
// implementation so display_p4 requires no cross-repo dependency.
// Opcode encoding and register layout are identical to gfx_vm.h so that the
// same Ruby-side GfxBlock bytecode compiler drives both backends.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define DISPLAY_P4_VM_REG_COUNT         16
#define DISPLAY_P4_VM_MAX_PROGS         16
#define DISPLAY_P4_VM_MAX_BYTECODE_SIZE 384
#define DISPLAY_P4_VM_MAX_STRTABLE_SIZE 128
#define DISPLAY_P4_VM_INVALID_PROG_ID   0xFF

// Operand encoding (bit15=0: 15-bit signed immediate; bit15=1: register ref)
#define DISPLAY_P4_VM_OPERAND_IS_REG(w)  (((w) & 0x8000) != 0)
#define DISPLAY_P4_VM_OPERAND_REG_ID(w)  ((w) & 0x0F)
#define DISPLAY_P4_VM_OPERAND_IMM(w)     ((int16_t)(((int16_t)((w) << 1)) >> 1))

// Opcodes (must match Ruby-side GfxBlock bytecode compiler)
#define DISPLAY_P4_VM_OP_END             0x00
#define DISPLAY_P4_VM_OP_CLEAR           0x01
#define DISPLAY_P4_VM_OP_FILL_RECT       0x02
#define DISPLAY_P4_VM_OP_DRAW_RECT       0x03
#define DISPLAY_P4_VM_OP_FILL_ROUND_RECT 0x04
#define DISPLAY_P4_VM_OP_DRAW_ROUND_RECT 0x05
#define DISPLAY_P4_VM_OP_DRAW_LINE       0x06
#define DISPLAY_P4_VM_OP_FILL_CIRCLE     0x07
#define DISPLAY_P4_VM_OP_DRAW_TEXT       0x08

// LovyanGFX uses inline namespace v1 inside lgfx, so a plain forward
// declaration in namespace lgfx creates an ambiguous symbol.  Pull in the
// real definition instead.
#include <M5GFX.h>

// Initialize VM state (call once at boot, before any other VM function).
void display_p4_vm_init(void);

// Register a new program. Copies bytecode + strtable into internal storage.
// Returns prog_id in [0..MAX_PROGS-1] or INVALID_PROG_ID on error.
uint8_t display_p4_vm_define_prog(uint16_t canvas_id,
                                  const uint8_t *bytecode, uint16_t bytecode_len,
                                  const uint8_t *strtable, uint16_t strtable_len);

// Apply register updates and run the program, drawing into target sprite.
// reg_updates: packed [uint8_t reg_id, int16_t value(LE)] * reg_count.
// Returns 0 on success, -1 on error.
int display_p4_vm_exec_prog(uint16_t canvas_id, uint8_t prog_id,
                             const uint8_t *reg_updates, uint8_t reg_count,
                             lgfx::LGFX_Sprite *target);

// Release a single program slot. Safe to call on an already-free slot.
int display_p4_vm_delete_prog(uint8_t prog_id);

// Release all programs bound to canvas_id (called on canvas delete).
void display_p4_vm_delete_progs_by_canvas(uint16_t canvas_id);
