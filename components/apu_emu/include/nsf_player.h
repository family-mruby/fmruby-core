/*
 * NSF Player - Plays NSF files using 6502 CPU + APU emulator
 *
 * Pure C, no platform dependencies. Suitable for ESP32.
 */

#ifndef NSF_PLAYER_H
#define NSF_PLAYER_H

#include <stdint.h>
#include <stdbool.h>
#include "nes_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NSF header (128 bytes) */
typedef struct {
    char     tag[5];         /* "NESM\x1A" */
    uint8_t  version;
    uint8_t  total_songs;
    uint8_t  starting_song;  /* 1-based */
    uint16_t load_addr;
    uint16_t init_addr;
    uint16_t play_addr;
    char     song_name[32];
    char     artist[32];
    char     copyright[32];
    uint16_t ntsc_speed;     /* Play speed in microseconds (NTSC) */
    uint8_t  bankswitch[8];
    uint16_t pal_speed;
    uint8_t  pal_ntsc_flags; /* 0=NTSC, 1=PAL, 2=both */
    uint8_t  chip_flags;     /* Extra sound chips */
    uint8_t  reserved[4];
} nsf_header_t;

typedef struct {
    nes_cpu_t cpu;
    nsf_header_t header;
    bool loaded;
    bool playing;
    int current_song;        /* 0-based */
    uint32_t frames_played;
    bool use_bankswitch;

    /* ROM data for bankswitching */
    uint8_t *rom_data;
    uint32_t rom_size;
} nsf_player_t;

/* Load NSF file into player */
int nsf_player_load(nsf_player_t *player, const char *filename);

/* Load NSF from memory buffer */
int nsf_player_load_mem(nsf_player_t *player, const uint8_t *data, uint32_t size);

/* Start playing a song (0-based) */
int nsf_player_start(nsf_player_t *player, int song);

/* Execute one frame (call PLAY routine). Call at 60Hz. */
void nsf_player_tick(nsf_player_t *player);

/* Free resources */
void nsf_player_free(nsf_player_t *player);

#ifdef __cplusplus
}
#endif

#endif /* NSF_PLAYER_H */
