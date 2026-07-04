/*
 * Minimal 6502 CPU emulator for NSF playback
 *
 * Only implements the subset of instructions needed to run
 * NSF INIT/PLAY routines. No BCD mode, no IRQ/NMI vectors.
 */

#ifndef NES_CPU_H
#define NES_CPU_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NES_CPU_RAM_SIZE  0x800   /* 2KB internal RAM */
#define NES_CPU_MEM_SIZE  0x10000 /* 64KB address space */

/* CPU registers */
typedef struct {
    uint8_t  a;     /* Accumulator */
    uint8_t  x;     /* X index */
    uint8_t  y;     /* Y index */
    uint8_t  sp;    /* Stack pointer */
    uint16_t pc;    /* Program counter */
    uint8_t  p;     /* Status flags: NV-BDIZC */
} nes_cpu_regs_t;

/* Memory write callback (for APU register interception) */
typedef void (*nes_cpu_write_cb_t)(uint16_t addr, uint8_t data, void *userdata);

typedef struct {
    nes_cpu_regs_t regs;
    uint8_t mem[NES_CPU_MEM_SIZE];  /* Full 64KB address space */
    int cycles;                      /* Cycle counter */
    bool halted;                     /* Set when BRK or invalid opcode */

    /* Memory write callback for I/O interception */
    nes_cpu_write_cb_t write_cb;
    void *write_cb_userdata;
} nes_cpu_t;

/* Status flag bits */
#define FLAG_C  0x01  /* Carry */
#define FLAG_Z  0x02  /* Zero */
#define FLAG_I  0x04  /* Interrupt disable */
#define FLAG_D  0x08  /* Decimal (unused in NES) */
#define FLAG_B  0x10  /* Break */
#define FLAG_U  0x20  /* Unused (always 1) */
#define FLAG_V  0x40  /* Overflow */
#define FLAG_N  0x80  /* Negative */

/* Initialize CPU state */
void nes_cpu_init(nes_cpu_t *cpu);

/* Reset CPU (set PC from reset vector, init SP) */
void nes_cpu_reset(nes_cpu_t *cpu);

/* Set memory write callback */
void nes_cpu_set_write_cb(nes_cpu_t *cpu, nes_cpu_write_cb_t cb, void *userdata);

/* Execute one instruction. Returns number of cycles consumed. */
int nes_cpu_step(nes_cpu_t *cpu);

/* Execute until given cycle count or halt */
void nes_cpu_run(nes_cpu_t *cpu, int max_cycles);

/* JSR to address and run until RTS */
void nes_cpu_jsr(nes_cpu_t *cpu, uint16_t addr);

#ifdef __cplusplus
}
#endif

#endif /* NES_CPU_H */
