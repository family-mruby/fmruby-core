/*
 * nsf_end_probe: the NSF player's end of a song (doc/nsf_player_v2), on the
 * host, for tools/nsf_end_probe.rb.
 *
 * Built from components/apu_emu as the Tab5 builds it (apu_if_p4.c, the
 * nofrendo APU, the 6502, the NSF player) and driven the way
 * main/drivers/audio_p4/audio_p4_task.c drives it: each frame the NSF tick on
 * MAIN, apuif_process_mix with the MAIN volume times the song's fade, then
 * nsf_player_end_frame with the MAIN level. A track runs until the player
 * says it is over, or MAX_SECONDS.
 *
 *   nsf_end_probe FILE [TRACK|all] [MAX_SECONDS]
 *
 * One line per track (TRACK is 1-based, as the app shows it):
 *   RESULT track=N reason=silence|loop|limit|none end=SECONDS fade_at=SECONDS
 *   period=SECONDS run=SECONDS best_period=SECONDS best_run=SECONDS
 *   cost_avg_ns=.. cost_max_ns=..
 * cost_* is the time nsf_player_end_frame took per frame on this host.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "apu_if.h"
#include "nsf_player.h"

/* nofrendo's log functions (nes_apu.c may name them; nothing is logged). */
int log_init(void) { return 0; }
void log_shutdown(void) {}
int log_print(const char *s) { (void)s; return 0; }
int log_printf(const char *f, ...) { (void)f; return 0; }

/* NSF_END_DUMP=FILE: one line per frame, "hash level" (the frame hash the
 * loop finder stores and the MAIN level), for looking at a song by hand. */
static FILE *s_dump;

static const char *reason_name(int r)
{
    switch (r) {
    case NSF_END_SILENCE: return "silence";
    case NSF_END_LOOP:    return "loop";
    case NSF_END_LIMIT:   return "limit";
    default:              return "none";
    }
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int run_track(nsf_player_t *p, int track, int max_s)
{
    apuif_select(APUIF_INSTANCE_MAIN);
    if (nsf_player_start(p, track) != 0) return -1;

    uint32_t max_frames = (uint32_t)max_s * NSF_END_FPS;
    uint32_t f = 0;
    uint32_t fade_at = 0;
    uint64_t cost_sum = 0, cost_max = 0;
    int16_t buf[1024];
    while (f < max_frames && p->playing) {
        uint32_t gain = nsf_player_end_gain_q16(p);
        nsf_player_tick(p);
        memset(buf, 0, sizeof(buf));
        apuif_process_mix(buf, (int)(sizeof(buf) / sizeof(buf[0])), gain);
        if (s_dump) {
            uint32_t fh = p->end.frame_hash;
            fprintf(s_dump, "%u %u\n", (unsigned)((fh ^ (fh >> 16)) & 0xFFFF),
                    (unsigned)apuif_main_level());
        }
        uint64_t t0 = now_ns();
        nsf_player_end_frame(p, apuif_main_level());
        uint64_t dt = now_ns() - t0;
        cost_sum += dt;
        if (dt > cost_max) cost_max = dt;
        f++;
        if (!fade_at && p->end.fade_left) fade_at = f;
        if (p->end.done) {
            /* What the audio task does when a song is over */
            p->playing = false;
            apuif_write_reg(0x4015, 0x00);
        }
    }

    const nsf_end_t *e = &p->end;
    /* The player prints its own progress on stdout too: the result lines
     * start with "RESULT " so the caller can tell them apart. */
    printf("RESULT track=%d reason=%s end=%.2f fade_at=%.2f period=%.2f run=%.2f "
           "best_period=%.2f best_run=%.2f cost_avg_ns=%llu cost_max_ns=%llu\n",
           track + 1, e->done ? reason_name(e->reason) : "none",
           (double)f / NSF_END_FPS, (double)fade_at / NSF_END_FPS,
           (double)e->period / NSF_END_FPS, (double)e->run / NSF_END_FPS,
           (double)e->best_period / NSF_END_FPS, (double)e->best_run / NSF_END_FPS,
           (unsigned long long)(f ? cost_sum / f : 0), (unsigned long long)cost_max);
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: nsf_end_probe FILE [TRACK|all] [MAX_SECONDS]\n");
        return 2;
    }
    const char *which = argc > 2 ? argv[2] : "all";
    int max_s = argc > 3 ? atoi(argv[3]) : 600;

    const char *dump = getenv("NSF_END_DUMP");
    if (dump) s_dump = fopen(dump, "w");

    apuif_init();
    apuif_init_sub();

    nsf_player_t *p = (nsf_player_t *)calloc(1, sizeof(nsf_player_t));
    if (!p) return 1;
    if (nsf_player_load(p, argv[1]) != 0) {
        fprintf(stderr, "cannot load %s\n", argv[1]);
        return 1;
    }

    int first = 0, last = p->header.total_songs - 1;
    if (strcmp(which, "all") != 0) first = last = atoi(which) - 1;
    for (int t = first; t <= last; t++) {
        if (run_track(p, t, max_s) != 0) fprintf(stderr, "track %d: cannot start\n", t + 1);
    }
    nsf_player_free(p);
    free(p);
    if (s_dump) fclose(s_dump);
    return 0;
}
