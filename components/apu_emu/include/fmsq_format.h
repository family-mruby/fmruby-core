/*
 * FMSQ - Family mruby Sequence Format (v1)
 *
 * Compact binary format for NES APU music data.
 * See doc/fmsq_format.md for full specification.
 */

#ifndef FMSQ_FORMAT_H
#define FMSQ_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* File header */
typedef struct {
    char magic[4];          /* "FMSQ" */
    uint8_t version;        /* 1 */
    uint8_t flags;          /* reserved (0) */
    uint16_t frame_count;   /* total frames (little-endian) */
    uint16_t data_size;     /* command data size in bytes (little-endian) */
    uint16_t loop_offset;   /* loop point offset (0 = no loop, little-endian) */
} fmsq_header_t;            /* 12 bytes */

/* Channel IDs (cc field in command byte) */
#define FMSQ_CH_PULSE1    0
#define FMSQ_CH_PULSE2    1
#define FMSQ_CH_TRIANGLE  2
#define FMSQ_CH_NOISE     3

/* --- Command encoding --- */

/* WAIT: 0xxxxxxx (1-128 frames) */
#define FMSQ_IS_WAIT(cmd)       (((cmd) & 0x80) == 0)
#define FMSQ_WAIT_FRAMES(cmd)   (((cmd) & 0x7F) + 1)
#define FMSQ_MAKE_WAIT(frames)  ((uint8_t)((frames) - 1))  /* frames: 1-128 */
#define FMSQ_MAX_WAIT_FRAMES    128

/* Channel commands: 10ccxxxx */
#define FMSQ_CMD_NOTE_ON(ch)    (0x80 | ((ch) << 4))        /* 10cc0000 */
#define FMSQ_CMD_NOTE_OFF(ch)   (0x80 | ((ch) << 4) | 0x01) /* 10cc0001 */
#define FMSQ_CMD_PARAM(ch)      (0x80 | ((ch) << 4) | 0x02) /* 10cc0010 */

/* Extract channel from command byte */
#define FMSQ_CMD_CHANNEL(cmd)   (((cmd) >> 4) & 0x03)
/* Extract sub-command (low nibble) */
#define FMSQ_CMD_SUB(cmd)       ((cmd) & 0x0F)

/* DPCM commands: 1110xxxx */
#define FMSQ_CMD_DPCM_PLAY     0xE0  /* + RATE_FLAGS + ADDR + LENGTH */
#define FMSQ_CMD_DPCM_STOP     0xE1
#define FMSQ_CMD_DPCM_RAW      0xE2  /* + VALUE (7bit DAC) */

/*
 * REG_WRITE: Direct APU register write (preserves exact order and values)
 * 110aaaaa [DATA]
 * - aaaaa: register offset from $4000 (0-23 = $4000-$4017)
 * - DATA: value to write
 * Total: 2 bytes per write
 */
#define FMSQ_CMD_REG_WRITE_BASE  0xC0
#define FMSQ_CMD_REG_WRITE(offset) (FMSQ_CMD_REG_WRITE_BASE | ((offset) & 0x1F))
#define FMSQ_IS_REG_WRITE(cmd)     (((cmd) & 0xE0) == 0xC0)
#define FMSQ_REG_OFFSET(cmd)       ((cmd) & 0x1F)
#define FMSQ_REG_ADDR(cmd)         (0x4000 + FMSQ_REG_OFFSET(cmd))

/* META commands */
#define FMSQ_CMD_END            0xFE
#define FMSQ_CMD_LOOP           0xFF  /* + OFFSET_LO + OFFSET_HI */

/* --- PARAM masks --- */

/* Pulse PARAM mask bits */
#define FMSQ_PULSE_MASK_TIMER   0x01  /* TIMER_LO + TIMER_HI_LEN (2 bytes) */
#define FMSQ_PULSE_MASK_VOL     0x02  /* VOL_ENV (1 byte) */
#define FMSQ_PULSE_MASK_SWEEP   0x04  /* SWEEP (1 byte) */

/* Triangle PARAM mask bits */
#define FMSQ_TRI_MASK_TIMER     0x01  /* TIMER_LO + TIMER_HI_LEN (2 bytes) */
#define FMSQ_TRI_MASK_LINEAR    0x02  /* LINEAR (1 byte) */

/* Noise PARAM mask bits */
#define FMSQ_NOISE_MASK_PERIOD  0x01  /* PERIOD_MODE (1 byte) */
#define FMSQ_NOISE_MASK_VOL     0x02  /* VOL_ENV (1 byte) */

/* --- APU register addresses (for reference) --- */

/* Pulse 1: $4000-$4003 */
#define APU_PULSE1_VOL    0x4000
#define APU_PULSE1_SWEEP  0x4001
#define APU_PULSE1_LO     0x4002
#define APU_PULSE1_HI     0x4003

/* Pulse 2: $4004-$4007 */
#define APU_PULSE2_VOL    0x4004
#define APU_PULSE2_SWEEP  0x4005
#define APU_PULSE2_LO     0x4006
#define APU_PULSE2_HI     0x4007

/* Triangle: $4008, $400A-$400B */
#define APU_TRI_LINEAR    0x4008
#define APU_TRI_LO        0x400A
#define APU_TRI_HI        0x400B

/* Noise: $400C, $400E-$400F */
#define APU_NOISE_VOL     0x400C
#define APU_NOISE_LO      0x400E
#define APU_NOISE_HI      0x400F

/* DMC: $4010-$4013 */
#define APU_DMC_FREQ      0x4010
#define APU_DMC_RAW       0x4011
#define APU_DMC_START     0x4012
#define APU_DMC_LEN       0x4013

/* Status: $4015 */
#define APU_STATUS        0x4015

#ifdef __cplusplus
}
#endif

#endif /* FMSQ_FORMAT_H */
