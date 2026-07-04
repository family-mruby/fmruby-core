/*
 * fmsq_player - FMSQ file playback engine
 *
 * Parses FMSQ binary files and drives the Nofrendo APU emulator
 * by interpreting commands and writing APU registers each frame.
 */

#ifndef FMSQ_PLAYER_H
#define FMSQ_PLAYER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* File data (owned) */
    uint8_t *data;
    uint32_t data_size;

    /* Header fields */
    uint16_t frame_count;
    uint16_t loop_offset;

    /* Playback state */
    uint32_t pc;            /* program counter into command data */
    int wait_frames;        /* remaining wait frames */
    bool playing;
    bool looped;            /* true if loop command was encountered */
    uint32_t frames_played;
} fmsq_player_t;

/* Load FMSQ file. Returns 0 on success. */
int fmsq_player_load(fmsq_player_t *player, const char *filename);

/* Load FMSQ from memory buffer. Returns 0 on success. */
int fmsq_player_load_from_memory(fmsq_player_t *player, const uint8_t *data, uint32_t size);

/* Free loaded data. */
void fmsq_player_free(fmsq_player_t *player);

/* Reset playback to beginning. */
void fmsq_player_reset(fmsq_player_t *player);

/*
 * Advance one frame (1/60s).
 * Processes commands until a WAIT is hit, writing APU registers.
 * Returns false when playback has ended.
 */
bool fmsq_player_tick(fmsq_player_t *player);

#ifdef __cplusplus
}
#endif

#endif /* FMSQ_PLAYER_H */
