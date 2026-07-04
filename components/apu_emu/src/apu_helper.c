#include "apu_helper.h"
#include "apu_if.h"

/* NES APU CPU clock (NTSC) */
#define APU_CPU_CLOCK 1789773

/* --- Pulse channels --- */

void apu_pulse_note_on(uint8_t ch, uint16_t freq,
                       uint8_t volume, uint8_t duty, uint8_t sweep) {
    if (freq == 0) return;
    if (ch != APU_CH_PULSE1 && ch != APU_CH_PULSE2) return;

    uint16_t timer = APU_CPU_CLOCK / (16 * freq) - 1;
    uint16_t base = (ch == APU_CH_PULSE1) ? 0x4000 : 0x4004;
    uint8_t status_bit = (ch == APU_CH_PULSE1) ? 0x01 : 0x02;
    uint8_t vol = volume & 0x0F;

    /* Enable channel */
    apuif_write_reg(0x4015, apuif_read_reg(0x4015) | status_bit);
    /* Volume: duty(2) + length halt(1) + constant vol(1) + vol(4) */
    apuif_write_reg(base + 0, ((duty & 0x03) << 6) | 0x30 | vol);
    /* Sweep */
    apuif_write_reg(base + 1, sweep);
    /* Timer low */
    apuif_write_reg(base + 2, timer & 0xFF);
    /* Timer high + length counter load */
    apuif_write_reg(base + 3, 0xF8 | ((timer >> 8) & 0x07));
}

void apu_pulse_note_off(uint8_t ch) {
    if (ch != APU_CH_PULSE1 && ch != APU_CH_PULSE2) return;

    uint16_t base = (ch == APU_CH_PULSE1) ? 0x4000 : 0x4004;
    apuif_write_reg(base + 0, 0x30); /* vol=0, constant volume */
}

/* --- Triangle channel --- */

void apu_triangle_note_on(uint16_t freq) {
    if (freq == 0) return;

    uint16_t timer = APU_CPU_CLOCK / (32 * freq) - 1;

    /* Enable triangle */
    apuif_write_reg(0x4015, apuif_read_reg(0x4015) | 0x04);
    /* Linear counter: halt=1 + counter=0x7F for sustained */
    apuif_write_reg(0x4008, 0xFF);
    /* Timer low */
    apuif_write_reg(0x400A, timer & 0xFF);
    /* Timer high + length counter load */
    apuif_write_reg(0x400B, 0xF8 | ((timer >> 8) & 0x07));
}

void apu_triangle_note_off(void) {
    /* Disable triangle channel */
    apuif_write_reg(0x4015, apuif_read_reg(0x4015) & ~0x04);
}

/* --- Noise channel --- */

void apu_noise_note_on(uint8_t period, uint8_t mode, uint8_t volume) {
    uint8_t vol = volume & 0x0F;

    /* Enable noise */
    apuif_write_reg(0x4015, apuif_read_reg(0x4015) | 0x08);
    /* Volume: length halt + constant vol + volume */
    apuif_write_reg(0x400C, 0x30 | vol);
    /* Period: bit7 = short mode, bits 0-3 = period */
    apuif_write_reg(0x400E, (mode ? 0x80 : 0x00) | (period & 0x0F));
    /* Length counter load */
    apuif_write_reg(0x400F, 0xF8);
}

void apu_noise_note_off(void) {
    apuif_write_reg(0x400C, 0x30); /* vol=0, constant volume */
}
