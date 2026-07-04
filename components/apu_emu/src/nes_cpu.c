/*
 * Minimal 6502 CPU emulator for NSF playback
 *
 * Pure C, no platform dependencies. Suitable for ESP32.
 * Implements all official 6502 opcodes (no BCD, no undocumented ops).
 */

#include "nes_cpu.h"
#include <string.h>

/* Memory access */
static inline uint8_t mem_read(nes_cpu_t *cpu, uint16_t addr)
{
    return cpu->mem[addr];
}

static inline void mem_write(nes_cpu_t *cpu, uint16_t addr, uint8_t val)
{
    cpu->mem[addr] = val;
    if (cpu->write_cb) {
        cpu->write_cb(addr, val, cpu->write_cb_userdata);
    }
}

static inline uint16_t mem_read16(nes_cpu_t *cpu, uint16_t addr)
{
    return mem_read(cpu, addr) | ((uint16_t)mem_read(cpu, addr + 1) << 8);
}

/* Stack operations */
static inline void push8(nes_cpu_t *cpu, uint8_t val)
{
    cpu->mem[0x0100 + cpu->regs.sp] = val;
    cpu->regs.sp--;
}

static inline uint8_t pull8(nes_cpu_t *cpu)
{
    cpu->regs.sp++;
    return cpu->mem[0x0100 + cpu->regs.sp];
}

static inline void push16(nes_cpu_t *cpu, uint16_t val)
{
    push8(cpu, (uint8_t)(val >> 8));
    push8(cpu, (uint8_t)(val & 0xFF));
}

static inline uint16_t pull16(nes_cpu_t *cpu)
{
    uint16_t lo = pull8(cpu);
    uint16_t hi = pull8(cpu);
    return (hi << 8) | lo;
}

/* Flag helpers */
static inline void set_flag(nes_cpu_t *cpu, uint8_t flag, bool val)
{
    if (val) cpu->regs.p |= flag;
    else     cpu->regs.p &= ~flag;
}

static inline bool get_flag(nes_cpu_t *cpu, uint8_t flag)
{
    return (cpu->regs.p & flag) != 0;
}

static inline void update_nz(nes_cpu_t *cpu, uint8_t val)
{
    set_flag(cpu, FLAG_Z, val == 0);
    set_flag(cpu, FLAG_N, (val & 0x80) != 0);
}

/* Addressing modes - return address */
static inline uint16_t addr_imm(nes_cpu_t *cpu) { return cpu->regs.pc++; }
static inline uint16_t addr_zp(nes_cpu_t *cpu) { return mem_read(cpu, cpu->regs.pc++); }
static inline uint16_t addr_zpx(nes_cpu_t *cpu) { return (mem_read(cpu, cpu->regs.pc++) + cpu->regs.x) & 0xFF; }
static inline uint16_t addr_zpy(nes_cpu_t *cpu) { return (mem_read(cpu, cpu->regs.pc++) + cpu->regs.y) & 0xFF; }
static inline uint16_t addr_abs(nes_cpu_t *cpu) { uint16_t a = mem_read16(cpu, cpu->regs.pc); cpu->regs.pc += 2; return a; }
static inline uint16_t addr_abx(nes_cpu_t *cpu) { uint16_t a = mem_read16(cpu, cpu->regs.pc) + cpu->regs.x; cpu->regs.pc += 2; return a; }
static inline uint16_t addr_aby(nes_cpu_t *cpu) { uint16_t a = mem_read16(cpu, cpu->regs.pc) + cpu->regs.y; cpu->regs.pc += 2; return a; }

static inline uint16_t addr_izx(nes_cpu_t *cpu)
{
    uint8_t zp = (mem_read(cpu, cpu->regs.pc++) + cpu->regs.x) & 0xFF;
    return mem_read(cpu, zp) | ((uint16_t)mem_read(cpu, (zp + 1) & 0xFF) << 8);
}

static inline uint16_t addr_izy(nes_cpu_t *cpu)
{
    uint8_t zp = mem_read(cpu, cpu->regs.pc++);
    uint16_t base = mem_read(cpu, zp) | ((uint16_t)mem_read(cpu, (zp + 1) & 0xFF) << 8);
    return base + cpu->regs.y;
}

/* ALU operations */
static void op_adc(nes_cpu_t *cpu, uint8_t val)
{
    uint16_t sum = cpu->regs.a + val + (get_flag(cpu, FLAG_C) ? 1 : 0);
    set_flag(cpu, FLAG_C, sum > 0xFF);
    set_flag(cpu, FLAG_V, (~(cpu->regs.a ^ val) & (cpu->regs.a ^ sum) & 0x80) != 0);
    cpu->regs.a = (uint8_t)sum;
    update_nz(cpu, cpu->regs.a);
}

static void op_sbc(nes_cpu_t *cpu, uint8_t val)
{
    op_adc(cpu, ~val);
}

static void op_cmp(nes_cpu_t *cpu, uint8_t reg, uint8_t val)
{
    uint16_t diff = reg - val;
    set_flag(cpu, FLAG_C, reg >= val);
    update_nz(cpu, (uint8_t)diff);
}

void nes_cpu_init(nes_cpu_t *cpu)
{
    memset(cpu, 0, sizeof(*cpu));
    cpu->regs.sp = 0xFF;
    cpu->regs.p = FLAG_U | FLAG_I;
    cpu->write_cb = NULL;
    cpu->write_cb_userdata = NULL;
}

void nes_cpu_reset(nes_cpu_t *cpu)
{
    cpu->regs.sp = 0xFF;
    cpu->regs.p = FLAG_U | FLAG_I;
    cpu->regs.pc = mem_read16(cpu, 0xFFFC);
    cpu->cycles = 0;
    cpu->halted = false;
}

void nes_cpu_set_write_cb(nes_cpu_t *cpu, nes_cpu_write_cb_t cb, void *userdata)
{
    cpu->write_cb = cb;
    cpu->write_cb_userdata = userdata;
}

int nes_cpu_step(nes_cpu_t *cpu)
{
    if (cpu->halted) return 0;

    uint8_t opcode = mem_read(cpu, cpu->regs.pc++);
    int cycles = 2; /* default, overridden per instruction */
    uint16_t addr;
    uint8_t val, tmp;

    switch (opcode) {
    /* --- Load/Store --- */
    case 0xA9: val = mem_read(cpu, addr_imm(cpu)); cpu->regs.a = val; update_nz(cpu, val); break; /* LDA imm */
    case 0xA5: val = mem_read(cpu, addr_zp(cpu)); cpu->regs.a = val; update_nz(cpu, val); cycles=3; break; /* LDA zp */
    case 0xB5: val = mem_read(cpu, addr_zpx(cpu)); cpu->regs.a = val; update_nz(cpu, val); cycles=4; break; /* LDA zpx */
    case 0xAD: val = mem_read(cpu, addr_abs(cpu)); cpu->regs.a = val; update_nz(cpu, val); cycles=4; break; /* LDA abs */
    case 0xBD: val = mem_read(cpu, addr_abx(cpu)); cpu->regs.a = val; update_nz(cpu, val); cycles=4; break; /* LDA abx */
    case 0xB9: val = mem_read(cpu, addr_aby(cpu)); cpu->regs.a = val; update_nz(cpu, val); cycles=4; break; /* LDA aby */
    case 0xA1: val = mem_read(cpu, addr_izx(cpu)); cpu->regs.a = val; update_nz(cpu, val); cycles=6; break; /* LDA izx */
    case 0xB1: val = mem_read(cpu, addr_izy(cpu)); cpu->regs.a = val; update_nz(cpu, val); cycles=5; break; /* LDA izy */

    case 0xA2: val = mem_read(cpu, addr_imm(cpu)); cpu->regs.x = val; update_nz(cpu, val); break; /* LDX imm */
    case 0xA6: val = mem_read(cpu, addr_zp(cpu)); cpu->regs.x = val; update_nz(cpu, val); cycles=3; break; /* LDX zp */
    case 0xB6: val = mem_read(cpu, addr_zpy(cpu)); cpu->regs.x = val; update_nz(cpu, val); cycles=4; break; /* LDX zpy */
    case 0xAE: val = mem_read(cpu, addr_abs(cpu)); cpu->regs.x = val; update_nz(cpu, val); cycles=4; break; /* LDX abs */
    case 0xBE: val = mem_read(cpu, addr_aby(cpu)); cpu->regs.x = val; update_nz(cpu, val); cycles=4; break; /* LDX aby */

    case 0xA0: val = mem_read(cpu, addr_imm(cpu)); cpu->regs.y = val; update_nz(cpu, val); break; /* LDY imm */
    case 0xA4: val = mem_read(cpu, addr_zp(cpu)); cpu->regs.y = val; update_nz(cpu, val); cycles=3; break; /* LDY zp */
    case 0xB4: val = mem_read(cpu, addr_zpx(cpu)); cpu->regs.y = val; update_nz(cpu, val); cycles=4; break; /* LDY zpx */
    case 0xAC: val = mem_read(cpu, addr_abs(cpu)); cpu->regs.y = val; update_nz(cpu, val); cycles=4; break; /* LDY abs */
    case 0xBC: val = mem_read(cpu, addr_abx(cpu)); cpu->regs.y = val; update_nz(cpu, val); cycles=4; break; /* LDY abx */

    case 0x85: mem_write(cpu, addr_zp(cpu), cpu->regs.a); cycles=3; break; /* STA zp */
    case 0x95: mem_write(cpu, addr_zpx(cpu), cpu->regs.a); cycles=4; break; /* STA zpx */
    case 0x8D: mem_write(cpu, addr_abs(cpu), cpu->regs.a); cycles=4; break; /* STA abs */
    case 0x9D: mem_write(cpu, addr_abx(cpu), cpu->regs.a); cycles=5; break; /* STA abx */
    case 0x99: mem_write(cpu, addr_aby(cpu), cpu->regs.a); cycles=5; break; /* STA aby */
    case 0x81: mem_write(cpu, addr_izx(cpu), cpu->regs.a); cycles=6; break; /* STA izx */
    case 0x91: mem_write(cpu, addr_izy(cpu), cpu->regs.a); cycles=6; break; /* STA izy */

    case 0x86: mem_write(cpu, addr_zp(cpu), cpu->regs.x); cycles=3; break; /* STX zp */
    case 0x96: mem_write(cpu, addr_zpy(cpu), cpu->regs.x); cycles=4; break; /* STX zpy */
    case 0x8E: mem_write(cpu, addr_abs(cpu), cpu->regs.x); cycles=4; break; /* STX abs */

    case 0x84: mem_write(cpu, addr_zp(cpu), cpu->regs.y); cycles=3; break; /* STY zp */
    case 0x94: mem_write(cpu, addr_zpx(cpu), cpu->regs.y); cycles=4; break; /* STY zpx */
    case 0x8C: mem_write(cpu, addr_abs(cpu), cpu->regs.y); cycles=4; break; /* STY abs */

    /* --- Transfer --- */
    case 0xAA: cpu->regs.x = cpu->regs.a; update_nz(cpu, cpu->regs.x); break; /* TAX */
    case 0x8A: cpu->regs.a = cpu->regs.x; update_nz(cpu, cpu->regs.a); break; /* TXA */
    case 0xA8: cpu->regs.y = cpu->regs.a; update_nz(cpu, cpu->regs.y); break; /* TAY */
    case 0x98: cpu->regs.a = cpu->regs.y; update_nz(cpu, cpu->regs.a); break; /* TYA */
    case 0xBA: cpu->regs.x = cpu->regs.sp; update_nz(cpu, cpu->regs.x); break; /* TSX */
    case 0x9A: cpu->regs.sp = cpu->regs.x; break; /* TXS */

    /* --- Stack --- */
    case 0x48: push8(cpu, cpu->regs.a); cycles=3; break; /* PHA */
    case 0x68: cpu->regs.a = pull8(cpu); update_nz(cpu, cpu->regs.a); cycles=4; break; /* PLA */
    case 0x08: push8(cpu, cpu->regs.p | FLAG_B | FLAG_U); cycles=3; break; /* PHP */
    case 0x28: cpu->regs.p = (pull8(cpu) & ~FLAG_B) | FLAG_U; cycles=4; break; /* PLP */

    /* --- Arithmetic --- */
    case 0x69: op_adc(cpu, mem_read(cpu, addr_imm(cpu))); break; /* ADC imm */
    case 0x65: op_adc(cpu, mem_read(cpu, addr_zp(cpu))); cycles=3; break;
    case 0x75: op_adc(cpu, mem_read(cpu, addr_zpx(cpu))); cycles=4; break;
    case 0x6D: op_adc(cpu, mem_read(cpu, addr_abs(cpu))); cycles=4; break;
    case 0x7D: op_adc(cpu, mem_read(cpu, addr_abx(cpu))); cycles=4; break;
    case 0x79: op_adc(cpu, mem_read(cpu, addr_aby(cpu))); cycles=4; break;
    case 0x61: op_adc(cpu, mem_read(cpu, addr_izx(cpu))); cycles=6; break;
    case 0x71: op_adc(cpu, mem_read(cpu, addr_izy(cpu))); cycles=5; break;

    case 0xE9: op_sbc(cpu, mem_read(cpu, addr_imm(cpu))); break; /* SBC imm */
    case 0xE5: op_sbc(cpu, mem_read(cpu, addr_zp(cpu))); cycles=3; break;
    case 0xF5: op_sbc(cpu, mem_read(cpu, addr_zpx(cpu))); cycles=4; break;
    case 0xED: op_sbc(cpu, mem_read(cpu, addr_abs(cpu))); cycles=4; break;
    case 0xFD: op_sbc(cpu, mem_read(cpu, addr_abx(cpu))); cycles=4; break;
    case 0xF9: op_sbc(cpu, mem_read(cpu, addr_aby(cpu))); cycles=4; break;
    case 0xE1: op_sbc(cpu, mem_read(cpu, addr_izx(cpu))); cycles=6; break;
    case 0xF1: op_sbc(cpu, mem_read(cpu, addr_izy(cpu))); cycles=5; break;

    /* --- Compare --- */
    case 0xC9: op_cmp(cpu, cpu->regs.a, mem_read(cpu, addr_imm(cpu))); break; /* CMP imm */
    case 0xC5: op_cmp(cpu, cpu->regs.a, mem_read(cpu, addr_zp(cpu))); cycles=3; break;
    case 0xD5: op_cmp(cpu, cpu->regs.a, mem_read(cpu, addr_zpx(cpu))); cycles=4; break;
    case 0xCD: op_cmp(cpu, cpu->regs.a, mem_read(cpu, addr_abs(cpu))); cycles=4; break;
    case 0xDD: op_cmp(cpu, cpu->regs.a, mem_read(cpu, addr_abx(cpu))); cycles=4; break;
    case 0xD9: op_cmp(cpu, cpu->regs.a, mem_read(cpu, addr_aby(cpu))); cycles=4; break;
    case 0xC1: op_cmp(cpu, cpu->regs.a, mem_read(cpu, addr_izx(cpu))); cycles=6; break;
    case 0xD1: op_cmp(cpu, cpu->regs.a, mem_read(cpu, addr_izy(cpu))); cycles=5; break;

    case 0xE0: op_cmp(cpu, cpu->regs.x, mem_read(cpu, addr_imm(cpu))); break; /* CPX imm */
    case 0xE4: op_cmp(cpu, cpu->regs.x, mem_read(cpu, addr_zp(cpu))); cycles=3; break;
    case 0xEC: op_cmp(cpu, cpu->regs.x, mem_read(cpu, addr_abs(cpu))); cycles=4; break;

    case 0xC0: op_cmp(cpu, cpu->regs.y, mem_read(cpu, addr_imm(cpu))); break; /* CPY imm */
    case 0xC4: op_cmp(cpu, cpu->regs.y, mem_read(cpu, addr_zp(cpu))); cycles=3; break;
    case 0xCC: op_cmp(cpu, cpu->regs.y, mem_read(cpu, addr_abs(cpu))); cycles=4; break;

    /* --- Increment/Decrement --- */
    case 0xE6: addr=addr_zp(cpu); val=mem_read(cpu,addr)+1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=5; break; /* INC zp */
    case 0xF6: addr=addr_zpx(cpu); val=mem_read(cpu,addr)+1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=6; break;
    case 0xEE: addr=addr_abs(cpu); val=mem_read(cpu,addr)+1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=6; break;
    case 0xFE: addr=addr_abx(cpu); val=mem_read(cpu,addr)+1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=7; break;

    case 0xC6: addr=addr_zp(cpu); val=mem_read(cpu,addr)-1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=5; break; /* DEC zp */
    case 0xD6: addr=addr_zpx(cpu); val=mem_read(cpu,addr)-1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=6; break;
    case 0xCE: addr=addr_abs(cpu); val=mem_read(cpu,addr)-1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=6; break;
    case 0xDE: addr=addr_abx(cpu); val=mem_read(cpu,addr)-1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=7; break;

    case 0xE8: cpu->regs.x++; update_nz(cpu, cpu->regs.x); break; /* INX */
    case 0xCA: cpu->regs.x--; update_nz(cpu, cpu->regs.x); break; /* DEX */
    case 0xC8: cpu->regs.y++; update_nz(cpu, cpu->regs.y); break; /* INY */
    case 0x88: cpu->regs.y--; update_nz(cpu, cpu->regs.y); break; /* DEY */

    /* --- Logic --- */
    case 0x29: cpu->regs.a &= mem_read(cpu, addr_imm(cpu)); update_nz(cpu, cpu->regs.a); break; /* AND imm */
    case 0x25: cpu->regs.a &= mem_read(cpu, addr_zp(cpu)); update_nz(cpu, cpu->regs.a); cycles=3; break;
    case 0x35: cpu->regs.a &= mem_read(cpu, addr_zpx(cpu)); update_nz(cpu, cpu->regs.a); cycles=4; break;
    case 0x2D: cpu->regs.a &= mem_read(cpu, addr_abs(cpu)); update_nz(cpu, cpu->regs.a); cycles=4; break;
    case 0x3D: cpu->regs.a &= mem_read(cpu, addr_abx(cpu)); update_nz(cpu, cpu->regs.a); cycles=4; break;
    case 0x39: cpu->regs.a &= mem_read(cpu, addr_aby(cpu)); update_nz(cpu, cpu->regs.a); cycles=4; break;
    case 0x21: cpu->regs.a &= mem_read(cpu, addr_izx(cpu)); update_nz(cpu, cpu->regs.a); cycles=6; break;
    case 0x31: cpu->regs.a &= mem_read(cpu, addr_izy(cpu)); update_nz(cpu, cpu->regs.a); cycles=5; break;

    case 0x09: cpu->regs.a |= mem_read(cpu, addr_imm(cpu)); update_nz(cpu, cpu->regs.a); break; /* ORA imm */
    case 0x05: cpu->regs.a |= mem_read(cpu, addr_zp(cpu)); update_nz(cpu, cpu->regs.a); cycles=3; break;
    case 0x15: cpu->regs.a |= mem_read(cpu, addr_zpx(cpu)); update_nz(cpu, cpu->regs.a); cycles=4; break;
    case 0x0D: cpu->regs.a |= mem_read(cpu, addr_abs(cpu)); update_nz(cpu, cpu->regs.a); cycles=4; break;
    case 0x1D: cpu->regs.a |= mem_read(cpu, addr_abx(cpu)); update_nz(cpu, cpu->regs.a); cycles=4; break;
    case 0x19: cpu->regs.a |= mem_read(cpu, addr_aby(cpu)); update_nz(cpu, cpu->regs.a); cycles=4; break;
    case 0x01: cpu->regs.a |= mem_read(cpu, addr_izx(cpu)); update_nz(cpu, cpu->regs.a); cycles=6; break;
    case 0x11: cpu->regs.a |= mem_read(cpu, addr_izy(cpu)); update_nz(cpu, cpu->regs.a); cycles=5; break;

    case 0x49: cpu->regs.a ^= mem_read(cpu, addr_imm(cpu)); update_nz(cpu, cpu->regs.a); break; /* EOR imm */
    case 0x45: cpu->regs.a ^= mem_read(cpu, addr_zp(cpu)); update_nz(cpu, cpu->regs.a); cycles=3; break;
    case 0x55: cpu->regs.a ^= mem_read(cpu, addr_zpx(cpu)); update_nz(cpu, cpu->regs.a); cycles=4; break;
    case 0x4D: cpu->regs.a ^= mem_read(cpu, addr_abs(cpu)); update_nz(cpu, cpu->regs.a); cycles=4; break;
    case 0x5D: cpu->regs.a ^= mem_read(cpu, addr_abx(cpu)); update_nz(cpu, cpu->regs.a); cycles=4; break;
    case 0x59: cpu->regs.a ^= mem_read(cpu, addr_aby(cpu)); update_nz(cpu, cpu->regs.a); cycles=4; break;
    case 0x41: cpu->regs.a ^= mem_read(cpu, addr_izx(cpu)); update_nz(cpu, cpu->regs.a); cycles=6; break;
    case 0x51: cpu->regs.a ^= mem_read(cpu, addr_izy(cpu)); update_nz(cpu, cpu->regs.a); cycles=5; break;

    /* --- Shift/Rotate --- */
    case 0x0A: set_flag(cpu, FLAG_C, cpu->regs.a & 0x80); cpu->regs.a <<= 1; update_nz(cpu, cpu->regs.a); break; /* ASL A */
    case 0x06: addr=addr_zp(cpu); val=mem_read(cpu,addr); set_flag(cpu,FLAG_C,val&0x80); val<<=1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=5; break;
    case 0x16: addr=addr_zpx(cpu); val=mem_read(cpu,addr); set_flag(cpu,FLAG_C,val&0x80); val<<=1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=6; break;
    case 0x0E: addr=addr_abs(cpu); val=mem_read(cpu,addr); set_flag(cpu,FLAG_C,val&0x80); val<<=1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=6; break;
    case 0x1E: addr=addr_abx(cpu); val=mem_read(cpu,addr); set_flag(cpu,FLAG_C,val&0x80); val<<=1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=7; break;

    case 0x4A: set_flag(cpu, FLAG_C, cpu->regs.a & 0x01); cpu->regs.a >>= 1; update_nz(cpu, cpu->regs.a); break; /* LSR A */
    case 0x46: addr=addr_zp(cpu); val=mem_read(cpu,addr); set_flag(cpu,FLAG_C,val&0x01); val>>=1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=5; break;
    case 0x56: addr=addr_zpx(cpu); val=mem_read(cpu,addr); set_flag(cpu,FLAG_C,val&0x01); val>>=1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=6; break;
    case 0x4E: addr=addr_abs(cpu); val=mem_read(cpu,addr); set_flag(cpu,FLAG_C,val&0x01); val>>=1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=6; break;
    case 0x5E: addr=addr_abx(cpu); val=mem_read(cpu,addr); set_flag(cpu,FLAG_C,val&0x01); val>>=1; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=7; break;

    case 0x2A: tmp=get_flag(cpu,FLAG_C); set_flag(cpu,FLAG_C,cpu->regs.a&0x80); cpu->regs.a=(cpu->regs.a<<1)|tmp; update_nz(cpu,cpu->regs.a); break; /* ROL A */
    case 0x26: addr=addr_zp(cpu); val=mem_read(cpu,addr); tmp=get_flag(cpu,FLAG_C); set_flag(cpu,FLAG_C,val&0x80); val=(val<<1)|tmp; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=5; break;
    case 0x36: addr=addr_zpx(cpu); val=mem_read(cpu,addr); tmp=get_flag(cpu,FLAG_C); set_flag(cpu,FLAG_C,val&0x80); val=(val<<1)|tmp; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=6; break;
    case 0x2E: addr=addr_abs(cpu); val=mem_read(cpu,addr); tmp=get_flag(cpu,FLAG_C); set_flag(cpu,FLAG_C,val&0x80); val=(val<<1)|tmp; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=6; break;
    case 0x3E: addr=addr_abx(cpu); val=mem_read(cpu,addr); tmp=get_flag(cpu,FLAG_C); set_flag(cpu,FLAG_C,val&0x80); val=(val<<1)|tmp; mem_write(cpu,addr,val); update_nz(cpu,val); cycles=7; break;

    case 0x6A: tmp=get_flag(cpu,FLAG_C); set_flag(cpu,FLAG_C,cpu->regs.a&0x01); cpu->regs.a=(cpu->regs.a>>1)|(tmp<<7); update_nz(cpu,cpu->regs.a); break; /* ROR A */
    case 0x66: addr=addr_zp(cpu); val=mem_read(cpu,addr); tmp=get_flag(cpu,FLAG_C); set_flag(cpu,FLAG_C,val&0x01); val=(val>>1)|(tmp<<7); mem_write(cpu,addr,val); update_nz(cpu,val); cycles=5; break;
    case 0x76: addr=addr_zpx(cpu); val=mem_read(cpu,addr); tmp=get_flag(cpu,FLAG_C); set_flag(cpu,FLAG_C,val&0x01); val=(val>>1)|(tmp<<7); mem_write(cpu,addr,val); update_nz(cpu,val); cycles=6; break;
    case 0x6E: addr=addr_abs(cpu); val=mem_read(cpu,addr); tmp=get_flag(cpu,FLAG_C); set_flag(cpu,FLAG_C,val&0x01); val=(val>>1)|(tmp<<7); mem_write(cpu,addr,val); update_nz(cpu,val); cycles=6; break;
    case 0x7E: addr=addr_abx(cpu); val=mem_read(cpu,addr); tmp=get_flag(cpu,FLAG_C); set_flag(cpu,FLAG_C,val&0x01); val=(val>>1)|(tmp<<7); mem_write(cpu,addr,val); update_nz(cpu,val); cycles=7; break;

    /* --- BIT --- */
    case 0x24: val=mem_read(cpu,addr_zp(cpu)); set_flag(cpu,FLAG_Z,(cpu->regs.a&val)==0); set_flag(cpu,FLAG_V,val&0x40); set_flag(cpu,FLAG_N,val&0x80); cycles=3; break;
    case 0x2C: val=mem_read(cpu,addr_abs(cpu)); set_flag(cpu,FLAG_Z,(cpu->regs.a&val)==0); set_flag(cpu,FLAG_V,val&0x40); set_flag(cpu,FLAG_N,val&0x80); cycles=4; break;

    /* --- Branch --- */
    case 0x10: val=mem_read(cpu,cpu->regs.pc++); if(!get_flag(cpu,FLAG_N)) cpu->regs.pc+=(int8_t)val; cycles=2; break; /* BPL */
    case 0x30: val=mem_read(cpu,cpu->regs.pc++); if( get_flag(cpu,FLAG_N)) cpu->regs.pc+=(int8_t)val; cycles=2; break; /* BMI */
    case 0x50: val=mem_read(cpu,cpu->regs.pc++); if(!get_flag(cpu,FLAG_V)) cpu->regs.pc+=(int8_t)val; cycles=2; break; /* BVC */
    case 0x70: val=mem_read(cpu,cpu->regs.pc++); if( get_flag(cpu,FLAG_V)) cpu->regs.pc+=(int8_t)val; cycles=2; break; /* BVS */
    case 0x90: val=mem_read(cpu,cpu->regs.pc++); if(!get_flag(cpu,FLAG_C)) cpu->regs.pc+=(int8_t)val; cycles=2; break; /* BCC */
    case 0xB0: val=mem_read(cpu,cpu->regs.pc++); if( get_flag(cpu,FLAG_C)) cpu->regs.pc+=(int8_t)val; cycles=2; break; /* BCS */
    case 0xD0: val=mem_read(cpu,cpu->regs.pc++); if(!get_flag(cpu,FLAG_Z)) cpu->regs.pc+=(int8_t)val; cycles=2; break; /* BNE */
    case 0xF0: val=mem_read(cpu,cpu->regs.pc++); if( get_flag(cpu,FLAG_Z)) cpu->regs.pc+=(int8_t)val; cycles=2; break; /* BEQ */

    /* --- Jump/Call --- */
    case 0x4C: cpu->regs.pc = addr_abs(cpu); cycles=3; break; /* JMP abs */
    case 0x6C: /* JMP ind */
        addr = addr_abs(cpu);
        /* 6502 bug: wraps within page */
        cpu->regs.pc = mem_read(cpu, addr) |
                       ((uint16_t)mem_read(cpu, (addr & 0xFF00) | ((addr + 1) & 0xFF)) << 8);
        cycles = 5;
        break;
    case 0x20: /* JSR */
        addr = addr_abs(cpu);
        push16(cpu, cpu->regs.pc - 1);
        cpu->regs.pc = addr;
        cycles = 6;
        break;
    case 0x60: /* RTS */
        cpu->regs.pc = pull16(cpu) + 1;
        cycles = 6;
        break;
    case 0x40: /* RTI */
        cpu->regs.p = (pull8(cpu) & ~FLAG_B) | FLAG_U;
        cpu->regs.pc = pull16(cpu);
        cycles = 6;
        break;

    /* --- Flag --- */
    case 0x18: set_flag(cpu, FLAG_C, false); break; /* CLC */
    case 0x38: set_flag(cpu, FLAG_C, true); break;  /* SEC */
    case 0x58: set_flag(cpu, FLAG_I, false); break; /* CLI */
    case 0x78: set_flag(cpu, FLAG_I, true); break;  /* SEI */
    case 0xD8: set_flag(cpu, FLAG_D, false); break; /* CLD */
    case 0xF8: set_flag(cpu, FLAG_D, true); break;  /* SED */
    case 0xB8: set_flag(cpu, FLAG_V, false); break; /* CLV */

    /* --- NOP and BRK --- */
    case 0xEA: break; /* NOP */
    case 0x00: /* BRK */
        cpu->halted = true;
        cycles = 7;
        break;

    default:
        /* Unknown opcode: treat as NOP (1 byte, 2 cycles) */
        break;
    }

    cpu->cycles += cycles;
    return cycles;
}

void nes_cpu_run(nes_cpu_t *cpu, int max_cycles)
{
    int start = cpu->cycles;
    while (!cpu->halted && (cpu->cycles - start) < max_cycles) {
        nes_cpu_step(cpu);
    }
}

void nes_cpu_jsr(nes_cpu_t *cpu, uint16_t addr)
{
    /* Push a sentinel return address.
     * We push (0x4FF0 - 1) so RTS will return to 0x4FF0.
     * We place a BRK at 0x4FF0 to halt execution. */
    cpu->mem[0x4FF0] = 0x00; /* BRK */
    cpu->halted = false;

    push16(cpu, 0x4FF0 - 1);
    cpu->regs.pc = addr;

    /* Run until halted (BRK) or too many cycles */
    nes_cpu_run(cpu, 2000000); /* ~1 second at 1.79MHz */
}
