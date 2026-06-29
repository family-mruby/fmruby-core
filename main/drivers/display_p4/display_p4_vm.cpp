// GfxBlock bytecode VM for the ESP32-P4 / Tab5 display task.
// Logic mirrors fmruby-graphics-audio/main/graphics/gfx_vm.cpp so that the
// same Ruby GfxBlock programs run on both the WROVER and the P4 backends.

#include "display_p4_vm.h"

#include <cstring>

#include "fmrb_log.h"

static const char *TAG = "display_p4_vm";

typedef struct {
    bool     active;
    uint16_t canvas_id;
    uint16_t bytecode_len;
    uint16_t strtable_len;
    uint8_t  bytecode[DISPLAY_P4_VM_MAX_BYTECODE_SIZE];
    uint8_t  strtable[DISPLAY_P4_VM_MAX_STRTABLE_SIZE];
    int16_t  regs[DISPLAY_P4_VM_REG_COUNT];
} p4_prog_t;

static p4_prog_t g_progs[DISPLAY_P4_VM_MAX_PROGS];

void display_p4_vm_init(void) {
    memset(g_progs, 0, sizeof(g_progs));
    FMRB_LOGI(TAG, "VM init: max_progs=%d bc=%d st=%d",
              DISPLAY_P4_VM_MAX_PROGS,
              DISPLAY_P4_VM_MAX_BYTECODE_SIZE,
              DISPLAY_P4_VM_MAX_STRTABLE_SIZE);
}

uint8_t display_p4_vm_define_prog(uint16_t canvas_id,
                                  const uint8_t *bytecode, uint16_t bytecode_len,
                                  const uint8_t *strtable, uint16_t strtable_len) {
    if (bytecode_len > DISPLAY_P4_VM_MAX_BYTECODE_SIZE) {
        FMRB_LOGE(TAG, "DEFINE_PROG: bytecode_len=%u > max=%d",
                  bytecode_len, DISPLAY_P4_VM_MAX_BYTECODE_SIZE);
        return DISPLAY_P4_VM_INVALID_PROG_ID;
    }
    if (strtable_len > DISPLAY_P4_VM_MAX_STRTABLE_SIZE) {
        FMRB_LOGE(TAG, "DEFINE_PROG: strtable_len=%u > max=%d",
                  strtable_len, DISPLAY_P4_VM_MAX_STRTABLE_SIZE);
        return DISPLAY_P4_VM_INVALID_PROG_ID;
    }

    for (uint8_t i = 0; i < DISPLAY_P4_VM_MAX_PROGS; i++) {
        if (!g_progs[i].active) {
            p4_prog_t *p = &g_progs[i];
            p->active        = true;
            p->canvas_id     = canvas_id;
            p->bytecode_len  = bytecode_len;
            p->strtable_len  = strtable_len;
            if (bytecode_len > 0 && bytecode) {
                memcpy(p->bytecode, bytecode, bytecode_len);
            }
            if (strtable_len > 0 && strtable) {
                memcpy(p->strtable, strtable, strtable_len);
            }
            memset(p->regs, 0, sizeof(p->regs));
            FMRB_LOGI(TAG, "DEFINE_PROG ok: id=%u canvas=%u bc=%u st=%u",
                      i, canvas_id, bytecode_len, strtable_len);
            return i;
        }
    }
    FMRB_LOGE(TAG, "DEFINE_PROG: pool full");
    return DISPLAY_P4_VM_INVALID_PROG_ID;
}

int display_p4_vm_delete_prog(uint8_t prog_id) {
    if (prog_id >= DISPLAY_P4_VM_MAX_PROGS) return -1;
    if (!g_progs[prog_id].active) return 0;
    g_progs[prog_id].active = false;
    FMRB_LOGI(TAG, "DELETE_PROG: id=%u", prog_id);
    return 0;
}

void display_p4_vm_delete_progs_by_canvas(uint16_t canvas_id) {
    for (uint8_t i = 0; i < DISPLAY_P4_VM_MAX_PROGS; i++) {
        if (g_progs[i].active && g_progs[i].canvas_id == canvas_id) {
            g_progs[i].active = false;
            FMRB_LOGI(TAG, "DELETE_PROG (by canvas): id=%u canvas=%u", i, canvas_id);
        }
    }
}

// Resolve a string from the strtable by sequential index.
// Format: [len(1)][chars(len)] repeated. Returns NULL if not found.
static const char *lookup_str(const p4_prog_t *p, uint16_t str_id, uint8_t *out_len) {
    uint16_t pos = 0;
    uint16_t idx = 0;
    while (pos < p->strtable_len) {
        uint8_t len = p->strtable[pos++];
        if (pos + len > p->strtable_len) return nullptr;
        if (idx == str_id) {
            *out_len = len;
            return (const char *)&p->strtable[pos];
        }
        pos += len;
        idx++;
    }
    return nullptr;
}

static void vm_execute(p4_prog_t *prog, lgfx::LGFX_Sprite *target) {
    const uint8_t *pc  = prog->bytecode;
    const uint8_t *end = pc + prog->bytecode_len;

    while (pc < end) {
        uint8_t opcode = *pc++;
        if (pc >= end) break;
        uint8_t nops = *pc++;

        if (nops > 8) {
            FMRB_LOGE(TAG, "vm_execute: nops=%u > 8", nops);
            return;
        }
        if (pc + nops * 2 > end) {
            FMRB_LOGE(TAG, "vm_execute: truncated operand stream");
            return;
        }

        int16_t args[8];
        for (uint8_t i = 0; i < nops; i++) {
            uint16_t w = (uint16_t)pc[0] | ((uint16_t)pc[1] << 8);
            pc += 2;
            if (DISPLAY_P4_VM_OPERAND_IS_REG(w)) {
                args[i] = prog->regs[DISPLAY_P4_VM_OPERAND_REG_ID(w)];
            } else {
                args[i] = DISPLAY_P4_VM_OPERAND_IMM(w);
            }
        }

        switch (opcode) {
        case DISPLAY_P4_VM_OP_END:
            return;
        case DISPLAY_P4_VM_OP_CLEAR:
            target->fillScreen((uint8_t)args[0]);
            break;
        case DISPLAY_P4_VM_OP_FILL_RECT:
            target->fillRect(args[0], args[1], args[2], args[3], (uint8_t)args[4]);
            break;
        case DISPLAY_P4_VM_OP_DRAW_RECT:
            target->drawRect(args[0], args[1], args[2], args[3], (uint8_t)args[4]);
            break;
        case DISPLAY_P4_VM_OP_FILL_ROUND_RECT:
            target->fillRoundRect(args[0], args[1], args[2], args[3], args[4], (uint8_t)args[5]);
            break;
        case DISPLAY_P4_VM_OP_DRAW_ROUND_RECT:
            target->drawRoundRect(args[0], args[1], args[2], args[3], args[4], (uint8_t)args[5]);
            break;
        case DISPLAY_P4_VM_OP_DRAW_LINE:
            target->drawLine(args[0], args[1], args[2], args[3], (uint8_t)args[4]);
            break;
        case DISPLAY_P4_VM_OP_FILL_CIRCLE:
            target->fillCircle(args[0], args[1], args[2], (uint8_t)args[3]);
            break;
        case DISPLAY_P4_VM_OP_DRAW_TEXT: {
            // args: x, y, color, str_id
            uint8_t str_len = 0;
            const char *str = lookup_str(prog, (uint16_t)args[3], &str_len);
            if (str) {
                char buf[64];
                if (str_len >= sizeof(buf)) str_len = (uint8_t)(sizeof(buf) - 1);
                memcpy(buf, str, str_len);
                buf[str_len] = '\0';
                target->setTextColor((uint8_t)args[2]);
                target->setCursor(args[0], args[1]);
                target->print(buf);
            }
            break;
        }
        default:
            FMRB_LOGW(TAG, "vm_execute: unknown opcode 0x%02X", opcode);
            return;
        }
    }
}

int display_p4_vm_exec_prog(uint16_t canvas_id, uint8_t prog_id,
                             const uint8_t *reg_updates, uint8_t reg_count,
                             lgfx::LGFX_Sprite *target) {
    if (prog_id >= DISPLAY_P4_VM_MAX_PROGS) {
        FMRB_LOGE(TAG, "EXEC_PROG: invalid id=%u", prog_id);
        return -1;
    }
    p4_prog_t *p = &g_progs[prog_id];
    if (!p->active) {
        FMRB_LOGE(TAG, "EXEC_PROG: prog_id=%u not defined", prog_id);
        return -1;
    }
    if (p->canvas_id != canvas_id) {
        FMRB_LOGW(TAG, "EXEC_PROG: canvas mismatch (prog=%u req=%u)", p->canvas_id, canvas_id);
    }
    if (!target) {
        FMRB_LOGE(TAG, "EXEC_PROG: null target");
        return -1;
    }

    for (uint8_t i = 0; i < reg_count; i++) {
        uint8_t  reg_id = reg_updates[i * 3 + 0];
        int16_t  val    = (int16_t)((uint16_t)reg_updates[i * 3 + 1]
                                  | ((uint16_t)reg_updates[i * 3 + 2] << 8));
        if (reg_id < DISPLAY_P4_VM_REG_COUNT) {
            p->regs[reg_id] = val;
        }
    }

    vm_execute(p, target);
    return 0;
}
