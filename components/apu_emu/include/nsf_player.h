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

/*
 * End of a song (fmruby-core doc/nsf_player_v2).
 *
 * An NSF says nothing about how long a song is, so the player watches it
 * play and decides:
 *   - silence: the MAIN instance's output stays flat for NSF_END_SILENCE_FRAMES
 *     (the song stopped by itself; it ends at once, no fade);
 *   - loop: the APU register writes of each frame are hashed, and when the
 *     stream of frame hashes starts repeating with a period P, the repeats are
 *     counted; after NSF_END_LAPS laps the song fades out;
 *   - limit: a song whose loop was not found fades out after
 *     NSF_END_LIMIT_FRAMES (a loop being counted may run on to
 *     NSF_END_HARD_LIMIT_FRAMES, so a long loop is not cut mid-lap).
 *
 * The loop finder keeps one 16-bit hash per frame (NSF_END_HIST_FRAMES of
 * them, so a loop of up to about two minutes is found) and a direct-mapped
 * table of "anchors": a rolling fingerprint of the last NSF_END_WINDOW frame
 * hashes, kept for about one frame in sixteen (chosen by the fingerprint's
 * own bits, so the same place in the song is an anchor on every lap) with the
 * last NSF_END_ANCHOR_DEPTH places it was seen. An anchor seen before gives
 * candidate periods, and the one repeating longest is taken; the candidate is checked backwards
 * once and then one comparison per frame, with room for a frame early or late
 * now and then (a driver that keeps its tempo in fractions of a frame). Both buffers come from
 * apuemu_malloc (PSRAM on the devices); without them only the silence and
 * the limit work.
 */
#define NSF_END_FPS                60
#define NSF_END_SILENCE_FRAMES     (3 * NSF_END_FPS)
#define NSF_END_SILENCE_LEVEL      512      /* peak-to-peak in a frame, 16-bit */
#define NSF_END_LAPS               2
#define NSF_END_MIN_LOOP_FRAMES    (15 * NSF_END_FPS)
#define NSF_END_LIMIT_FRAMES       (180 * NSF_END_FPS)
#define NSF_END_HARD_LIMIT_FRAMES  (360 * NSF_END_FPS)
#define NSF_END_FADE_FRAMES        (5 * NSF_END_FPS)
#define NSF_END_HIST_FRAMES        16384    /* power of two */
#define NSF_END_WINDOW             32
#define NSF_END_MIN_CHANGES        4        /* in a window, to be an anchor */
#define NSF_END_ANCHORS            1024     /* power of two */
#define NSF_END_ANCHOR_DEPTH       4        /* earlier places kept per anchor */
#define NSF_END_MISS_COST          8        /* a frame that does not repeat */
#define NSF_END_MISS_LIMIT         64       /* drop the candidate past this */

typedef enum {
    NSF_END_NONE = 0,
    NSF_END_SILENCE = 1,
    NSF_END_LOOP = 2,
    NSF_END_LIMIT = 3
} nsf_end_reason_t;

typedef struct {
    uint16_t *hist;          /* frame hashes, a ring of NSF_END_HIST_FRAMES */
    uint32_t *anchor_f;      /* anchor fingerprints, NSF_END_ANCHORS */
    uint16_t *anchor_t;      /* where each was seen, NSF_END_ANCHOR_DEPTH stamps */
    uint32_t frame_hash;     /* this frame's writes, being hashed */
    uint32_t roll;           /* fingerprint of the last NSF_END_WINDOW hashes */
    uint32_t roll_pow;       /* the fingerprint's base to the window's power */
    uint16_t changes;        /* hash changes inside the window */
    uint32_t frames;         /* frames seen since the start */
    uint32_t silent_run;     /* consecutive flat frames */
    uint32_t period;         /* the loop being counted, frames (0 = none) */
    uint32_t run;            /* frames that repeated at that period */
    uint16_t miss;           /* the candidate's mismatches, leaky */
    uint32_t best_period;    /* the period of the longest run so far */
    uint32_t best_run;
    uint32_t fade_left;      /* frames left in the fade */
    uint8_t  reason;         /* nsf_end_reason_t; NONE until it is decided */
    bool     done;           /* over: faded out, or fell silent */
} nsf_end_t;

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

    nsf_end_t end;
} nsf_player_t;

/* Load NSF file into player */
int nsf_player_load(nsf_player_t *player, const char *filename);

/* Load NSF from memory buffer */
int nsf_player_load_mem(nsf_player_t *player, const uint8_t *data, uint32_t size);

/* Start playing a song (0-based) */
int nsf_player_start(nsf_player_t *player, int song);

/* Execute one frame (call PLAY routine). Call at 60Hz. */
void nsf_player_tick(nsf_player_t *player);

/* After each frame's mix: feed the end detector. level is the peak-to-peak
 * of the MAIN instance's samples in that frame (apuif_main_level()). */
void nsf_player_end_frame(nsf_player_t *player, uint32_t level);

/* The song's own gain for the next frame, Q16 (65536 = unity). Below unity
 * only while it fades out; multiply it into the MAIN volume. */
uint32_t nsf_player_end_gain_q16(const nsf_player_t *player);

/* Free resources */
void nsf_player_free(nsf_player_t *player);

#ifdef __cplusplus
}
#endif

#endif /* NSF_PLAYER_H */
