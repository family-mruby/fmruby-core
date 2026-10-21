/*
 * apu_render: Family mruby's APU sound on the host, for tools/mahjong_sounds.rb.
 *
 * Built from components/apu_emu as the Tab5 builds it (apu_if_p4.c, the
 * nofrendo APU, the 6502 and the NSF player, apu_helper.c) and driven the
 * way main/drivers/audio_p4/audio_p4_task.c drives it: 60 frames a second,
 * each frame first the players' ticks (the NSF on MAIN), then
 * apuif_process_mix (MAIN at unity, plus SUB). A frame is 262 or 263 mono
 * samples at 15720 Hz. Output: raw signed 16-bit little-endian samples.
 *
 *   apu_render nsf FILE TRACK FRAMES OUT.raw HASHES.txt
 *     plays an NSF track on MAIN for FRAMES frames; HASHES.txt gets one
 *     line per frame, a hash of the APU register writes of that frame
 *     (the caller finds the tune's loop in them).
 *   apu_render notes FRAMES OUT.raw < EVENTS
 *     plays notes on SUB as audio_p4_engine_note_on / note_off do (the
 *     effects of FmrbAudio#note_on). EVENTS lines: "FRAME on CH FREQ VOL DUTY"
 *     or "FRAME off CH", in frame order; a frame's events are written
 *     before that frame is made.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "apu_if.h"
#include "apu_helper.h"
#include "nsf_player.h"

/* nofrendo's log functions (nes_apu.c may name them; nothing is logged). */
int log_init(void) { return 0; }
void log_shutdown(void) {}
int log_print(const char *s) { (void)s; return 0; }
int log_printf(const char *f, ...) { (void)f; return 0; }

/* Every register write of the players goes through here (-Wl,--wrap). */
void __real_apuif_write_reg(uint32_t address, uint8_t value);
static uint64_t s_hash = 1469598103934665603ULL;
void __wrap_apuif_write_reg(uint32_t address, uint8_t value)
{
    uint8_t b[3] = { (uint8_t)(address & 0xFF), (uint8_t)(address >> 8), value };
    for (int i = 0; i < 3; i++) {
        s_hash ^= b[i];
        s_hash *= 1099511628211ULL;
    }
    __real_apuif_write_reg(address, value);
}

static int frame_out(FILE *out)
{
    int16_t buf[1024];
    memset(buf, 0, sizeof(buf));
    int n = apuif_process_mix(buf, (int)(sizeof(buf) / sizeof(buf[0])), 65536u);
    if (n < 0) return -1;
    for (int i = 0; i < n; i++) {
        uint8_t le[2] = { (uint8_t)(buf[i] & 0xFF), (uint8_t)((uint16_t)buf[i] >> 8) };
        fwrite(le, 1, 2, out);
    }
    return n;
}

static int run_nsf(const char *file, int track, long frames, const char *raw, const char *hashes)
{
    static nsf_player_t player;
    FILE *out = fopen(raw, "wb");
    FILE *hs = fopen(hashes, "w");
    if (!out || !hs) { perror("open"); return 1; }
    /* The player prints its own notes on stdout; keep stdout for the caller. */
    if (!freopen("/dev/null", "w", stdout)) return 1;
    apuif_init();
    apuif_init_sub();
    if (nsf_player_load(&player, file) != 0) { fprintf(stderr, "cannot load %s\n", file); return 1; }
    apuif_select(APUIF_INSTANCE_MAIN);
    nsf_player_start(&player, track);
    for (long f = 0; f < frames; f++) {
        s_hash = 1469598103934665603ULL;
        apuif_select(APUIF_INSTANCE_MAIN);
        nsf_player_tick(&player);
        fprintf(hs, "%016llx\n", (unsigned long long)s_hash);
        if (frame_out(out) < 0) return 1;
    }
    fclose(out);
    fclose(hs);
    return 0;
}

/* audio_p4_engine_note_on / note_off, on SUB. */
static void note_on(int ch, int freq, int vol, int duty)
{
    if (freq == 0 && ch != APU_CH_NOISE) return;
    apuif_select(APUIF_INSTANCE_SUB);
    switch (ch) {
    case APU_CH_PULSE1:
    case APU_CH_PULSE2: apu_pulse_note_on(ch, freq, vol, duty, 0); break;
    case APU_CH_TRIANGLE: apu_triangle_note_on(freq); break;
    case APU_CH_NOISE: apu_noise_note_on(freq & 0x0F, (freq & 0x80) ? 1 : 0, vol); break;
    }
}

static void note_off(int ch)
{
    apuif_select(APUIF_INSTANCE_SUB);
    switch (ch) {
    case APU_CH_PULSE1:
    case APU_CH_PULSE2: apu_pulse_note_off(ch); break;
    case APU_CH_TRIANGLE: apu_triangle_note_off(); break;
    case APU_CH_NOISE: apu_noise_note_off(); break;
    }
}

static int run_notes(long frames, const char *raw)
{
    FILE *out = fopen(raw, "wb");
    if (!out) { perror("open"); return 1; }
    apuif_init();
    apuif_init_sub();
    char line[256];
    long ef = -1;
    char kind[8];
    int ch = 0, freq = 0, vol = 0, duty = 0;
    int have = 0;
    for (long f = 0; f < frames; f++) {
        for (;;) {
            if (!have) {
                if (!fgets(line, sizeof(line), stdin)) { ef = -1; break; }
                int k = sscanf(line, "%ld %7s %d %d %d %d", &ef, kind, &ch, &freq, &vol, &duty);
                if (k < 3) continue;
                have = 1;
            }
            if (ef > f) break;
            if (strcmp(kind, "on") == 0) note_on(ch, freq, vol, duty);
            else note_off(ch);
            have = 0;
        }
        if (frame_out(out) < 0) return 1;
    }
    fclose(out);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 7 && strcmp(argv[1], "nsf") == 0)
        return run_nsf(argv[2], atoi(argv[3]), atol(argv[4]), argv[5], argv[6]);
    if (argc == 4 && strcmp(argv[1], "notes") == 0)
        return run_notes(atol(argv[2]), argv[3]);
    fprintf(stderr, "usage: apu_render nsf FILE TRACK FRAMES OUT.raw HASHES.txt\n"
                    "       apu_render notes FRAMES OUT.raw < EVENTS\n");
    return 2;
}
