/*
 * fmsq_player - FMSQ command interpreter
 *
 * Reads FMSQ binary commands and drives APU via apuif_write_reg().
 * Called once per frame (60Hz) from the main loop.
 */

#include "fmsq_player.h"
#include "fmsq_format.h"
#include "apu_if.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FMSQ_HEADER_SIZE 12

static uint8_t read_byte(fmsq_player_t *p)
{
    if (p->pc >= p->data_size) {
        p->playing = false;
        return FMSQ_CMD_END;
    }
    return p->data[p->pc++];
}

/* Apply NOTE_ON for pulse channel */
static void exec_pulse_note_on(fmsq_player_t *p, int ch)
{
    uint16_t base = (ch == FMSQ_CH_PULSE1) ? APU_PULSE1_LO : APU_PULSE2_LO;
    uint16_t base_vol = (ch == FMSQ_CH_PULSE1) ? APU_PULSE1_VOL : APU_PULSE2_VOL;
    uint16_t base_sweep = (ch == FMSQ_CH_PULSE1) ? APU_PULSE1_SWEEP : APU_PULSE2_SWEEP;

    uint8_t timer_lo    = read_byte(p);
    uint8_t timer_hi    = read_byte(p);
    uint8_t vol_env     = read_byte(p);
    uint8_t sweep       = read_byte(p);

    apuif_write_reg(base_vol, vol_env);
    apuif_write_reg(base_sweep, sweep);
    apuif_write_reg(base, timer_lo);
    apuif_write_reg(base + 1, timer_hi);
}

/* Apply NOTE_ON for triangle */
static void exec_tri_note_on(fmsq_player_t *p)
{
    uint8_t timer_lo = read_byte(p);
    uint8_t timer_hi = read_byte(p);
    uint8_t linear   = read_byte(p);

    apuif_write_reg(APU_TRI_LINEAR, linear);
    apuif_write_reg(APU_TRI_LO, timer_lo);
    apuif_write_reg(APU_TRI_HI, timer_hi);
}

/* Apply NOTE_ON for noise */
static void exec_noise_note_on(fmsq_player_t *p)
{
    uint8_t period_mode = read_byte(p);
    uint8_t vol_env     = read_byte(p);

    apuif_write_reg(APU_NOISE_VOL, vol_env);
    apuif_write_reg(APU_NOISE_LO, period_mode);
}

/* Apply PARAM for pulse */
static void exec_pulse_param(fmsq_player_t *p, int ch)
{
    uint16_t base = (ch == FMSQ_CH_PULSE1) ? APU_PULSE1_LO : APU_PULSE2_LO;
    uint16_t base_vol = (ch == FMSQ_CH_PULSE1) ? APU_PULSE1_VOL : APU_PULSE2_VOL;
    uint16_t base_sweep = (ch == FMSQ_CH_PULSE1) ? APU_PULSE1_SWEEP : APU_PULSE2_SWEEP;

    uint8_t mask = read_byte(p);

    if (mask & FMSQ_PULSE_MASK_TIMER) {
        uint8_t lo = read_byte(p);
        uint8_t hi = read_byte(p);
        apuif_write_reg(base, lo);
        apuif_write_reg(base + 1, hi);
    }
    if (mask & FMSQ_PULSE_MASK_VOL) {
        apuif_write_reg(base_vol, read_byte(p));
    }
    if (mask & FMSQ_PULSE_MASK_SWEEP) {
        apuif_write_reg(base_sweep, read_byte(p));
    }
}

/* Apply PARAM for triangle */
static void exec_tri_param(fmsq_player_t *p)
{
    uint8_t mask = read_byte(p);

    if (mask & FMSQ_TRI_MASK_TIMER) {
        uint8_t lo = read_byte(p);
        uint8_t hi = read_byte(p);
        apuif_write_reg(APU_TRI_LO, lo);
        apuif_write_reg(APU_TRI_HI, hi);
    }
    if (mask & FMSQ_TRI_MASK_LINEAR) {
        apuif_write_reg(APU_TRI_LINEAR, read_byte(p));
    }
}

/* Apply PARAM for noise */
static void exec_noise_param(fmsq_player_t *p)
{
    uint8_t mask = read_byte(p);

    if (mask & FMSQ_NOISE_MASK_PERIOD) {
        apuif_write_reg(APU_NOISE_LO, read_byte(p));
    }
    if (mask & FMSQ_NOISE_MASK_VOL) {
        apuif_write_reg(APU_NOISE_VOL, read_byte(p));
    }
}

/* Update $4015 status register: set or clear channel bit */
static void update_status(uint8_t *status, int ch, bool enable)
{
    uint8_t bit;
    switch (ch) {
    case FMSQ_CH_PULSE1:   bit = 0x01; break;
    case FMSQ_CH_PULSE2:   bit = 0x02; break;
    case FMSQ_CH_TRIANGLE: bit = 0x04; break;
    case FMSQ_CH_NOISE:    bit = 0x08; break;
    default: return;
    }

    if (enable)
        *status |= bit;
    else
        *status &= ~bit;

    apuif_write_reg(APU_STATUS, *status);
}

int fmsq_player_load(fmsq_player_t *player, const char *filename)
{
    memset(player, 0, sizeof(*player));

    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s'\n", filename);
        return -1;
    }

    /* Read header */
    uint8_t header[FMSQ_HEADER_SIZE];
    if (fread(header, 1, FMSQ_HEADER_SIZE, f) != FMSQ_HEADER_SIZE) {
        fprintf(stderr, "Error: Failed to read FMSQ header\n");
        fclose(f);
        return -1;
    }

    if (memcmp(header, "FMSQ", 4) != 0) {
        fprintf(stderr, "Error: Invalid FMSQ magic\n");
        fclose(f);
        return -1;
    }

    uint8_t version = header[4];
    if (version != 1) {
        fprintf(stderr, "Error: Unsupported FMSQ version %d\n", version);
        fclose(f);
        return -1;
    }

    player->frame_count = header[6] | (header[7] << 8);
    uint16_t cmd_size   = header[8] | (header[9] << 8);
    player->loop_offset = header[10] | (header[11] << 8);

    /* Read command data */
    player->data = (uint8_t *)apuemu_malloc(cmd_size);
    if (!player->data) {
        fprintf(stderr, "Error: Failed to allocate %d bytes\n", cmd_size);
        fclose(f);
        return -1;
    }

    size_t read_bytes = fread(player->data, 1, cmd_size, f);
    fclose(f);

    if (read_bytes != cmd_size) {
        fprintf(stderr, "Error: Expected %d bytes, read %zu\n", cmd_size, read_bytes);
        apuemu_free(player->data);
        player->data = NULL;
        return -1;
    }

    player->data_size = cmd_size;
    player->pc = 0;
    player->wait_frames = 0;
    player->playing = true;
    player->looped = false;
    player->frames_played = 0;

    printf("FMSQ loaded: version=%d, frames=%d, data=%d bytes, loop=%d\n",
           version, player->frame_count, cmd_size, player->loop_offset);

    return 0;
}

int fmsq_player_load_from_memory(fmsq_player_t *player, const uint8_t *data, uint32_t size)
{
    if (!data || size < FMSQ_HEADER_SIZE) {
        fprintf(stderr, "Error: FMSQ data too small (%u bytes)\n", size);
        return -1;
    }

    /* Free previous data if any */
    if (player->data) {
        apuemu_free(player->data);
        player->data = NULL;
    }
    memset(player, 0, sizeof(*player));

    /* Parse header */
    const uint8_t *header = data;

    if (memcmp(header, "FMSQ", 4) != 0) {
        fprintf(stderr, "Error: Invalid FMSQ magic\n");
        return -1;
    }

    uint8_t version = header[4];
    if (version != 1) {
        fprintf(stderr, "Error: Unsupported FMSQ version %d\n", version);
        return -1;
    }

    player->frame_count = header[6] | (header[7] << 8);
    uint16_t cmd_size   = header[8] | (header[9] << 8);
    player->loop_offset = header[10] | (header[11] << 8);

    if (FMSQ_HEADER_SIZE + cmd_size > size) {
        fprintf(stderr, "Error: FMSQ data truncated (need %d, got %u)\n",
                FMSQ_HEADER_SIZE + cmd_size, size);
        return -1;
    }

    /* Copy command data */
    player->data = (uint8_t *)apuemu_malloc(cmd_size);
    if (!player->data) {
        fprintf(stderr, "Error: Failed to allocate %d bytes\n", cmd_size);
        return -1;
    }

    memcpy(player->data, data + FMSQ_HEADER_SIZE, cmd_size);
    player->data_size = cmd_size;
    player->pc = 0;
    player->wait_frames = 0;
    player->playing = true;
    player->looped = false;
    player->frames_played = 0;

    printf("FMSQ loaded from memory: version=%d, frames=%d, data=%d bytes, loop=%d\n",
           version, player->frame_count, cmd_size, player->loop_offset);

    return 0;
}

void fmsq_player_free(fmsq_player_t *player)
{
    if (player->data) {
        apuemu_free(player->data);
        player->data = NULL;
    }
    player->playing = false;
}

void fmsq_player_reset(fmsq_player_t *player)
{
    player->pc = 0;
    player->wait_frames = 0;
    player->playing = (player->data != NULL);
    player->looped = false;
    player->frames_played = 0;

    /* Reset APU status */
    apuif_write_reg(APU_STATUS, 0x0F);
}

bool fmsq_player_tick(fmsq_player_t *player)
{
    if (!player->playing)
        return false;

    /* If still waiting, decrement and return */
    if (player->wait_frames > 0) {
        player->wait_frames--;
        player->frames_played++;
        return true;
    }

    /* Track current status register for NOTE_ON/OFF */
    static uint8_t status = 0x0F;

    /* Process commands until WAIT or END */
    while (player->playing) {
        uint8_t cmd = read_byte(player);

        if (FMSQ_IS_WAIT(cmd)) {
            player->wait_frames = FMSQ_WAIT_FRAMES(cmd) - 1;
            player->frames_played++;
            return true;
        }

        if (cmd == FMSQ_CMD_END) {
            player->playing = false;
            printf("FMSQ: playback ended at frame %u\n", player->frames_played);
            return false;
        }

        if (cmd == FMSQ_CMD_LOOP) {
            uint8_t lo = read_byte(player);
            uint8_t hi = read_byte(player);
            uint16_t offset = lo | (hi << 8);
            if (offset < player->data_size) {
                player->pc = offset;
                player->looped = true;
            } else {
                player->playing = false;
                return false;
            }
            continue;
        }

        /* DPCM commands */
        if (cmd == FMSQ_CMD_DPCM_PLAY) {
            uint8_t rate_flags = read_byte(player);
            uint8_t addr       = read_byte(player);
            uint8_t length     = read_byte(player);
            apuif_write_reg(APU_DMC_FREQ, rate_flags);
            apuif_write_reg(APU_DMC_START, addr);
            apuif_write_reg(APU_DMC_LEN, length);
            status |= 0x10;
            apuif_write_reg(APU_STATUS, status);
            continue;
        }

        if (cmd == FMSQ_CMD_DPCM_STOP) {
            status &= ~0x10;
            apuif_write_reg(APU_STATUS, status);
            continue;
        }

        if (cmd == FMSQ_CMD_DPCM_RAW) {
            uint8_t val = read_byte(player);
            apuif_write_reg(APU_DMC_RAW, val);
            continue;
        }

        /* REG_WRITE: 110aaaaa [DATA] -- direct APU register write */
        if (FMSQ_IS_REG_WRITE(cmd)) {
            uint8_t data = read_byte(player);
            apuif_write_reg(FMSQ_REG_ADDR(cmd), data);
            continue;
        }

        /* Channel commands: 10ccxxxx */
        int ch = FMSQ_CMD_CHANNEL(cmd);
        int sub = FMSQ_CMD_SUB(cmd);

        switch (sub) {
        case 0: /* NOTE_ON */
            update_status(&status, ch, true);
            if (ch <= FMSQ_CH_PULSE2)
                exec_pulse_note_on(player, ch);
            else if (ch == FMSQ_CH_TRIANGLE)
                exec_tri_note_on(player);
            else if (ch == FMSQ_CH_NOISE)
                exec_noise_note_on(player);
            break;

        case 1: /* NOTE_OFF */
            update_status(&status, ch, false);
            break;

        case 2: /* PARAM */
            if (ch <= FMSQ_CH_PULSE2)
                exec_pulse_param(player, ch);
            else if (ch == FMSQ_CH_TRIANGLE)
                exec_tri_param(player);
            else if (ch == FMSQ_CH_NOISE)
                exec_noise_param(player);
            break;

        default:
            fprintf(stderr, "FMSQ: unknown sub-command 0x%02x at pc=%u\n",
                    cmd, player->pc - 1);
            break;
        }
    }

    return player->playing;
}
