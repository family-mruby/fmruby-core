#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NES APU channel IDs (matches FMRB_APU_CH_*) */
#define APU_CH_PULSE1    0
#define APU_CH_PULSE2    1
#define APU_CH_TRIANGLE  2
#define APU_CH_NOISE     3

/*
 * High-level NES APU helpers.
 * These wrap apuif_write_reg() to provide a readable API
 * for note-level operations. Caller must apuif_select()
 * the desired APU instance before calling.
 */

/* Pulse channels (ch = APU_CH_PULSE1 or APU_CH_PULSE2) */
void apu_pulse_note_on(uint8_t ch, uint16_t freq,
                       uint8_t volume, uint8_t duty, uint8_t sweep);
void apu_pulse_note_off(uint8_t ch);

/* Triangle channel */
void apu_triangle_note_on(uint16_t freq);
void apu_triangle_note_off(void);

/* Noise channel */
void apu_noise_note_on(uint8_t period, uint8_t mode, uint8_t volume);
void apu_noise_note_off(void);

#ifdef __cplusplus
}
#endif
