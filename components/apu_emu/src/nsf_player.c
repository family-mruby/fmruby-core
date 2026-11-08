/*
 * NSF Player - Plays NSF files using 6502 CPU + APU emulator
 *
 * Pure C, no platform dependencies. Suitable for ESP32.
 *
 * NSF format:
 * - 128-byte header with init/play addresses and bank info
 * - ROM data loaded at load_addr (or bank-switched)
 * - INIT routine called once with song# in A, PAL/NTSC in X
 * - PLAY routine called at 60Hz (NTSC) to generate audio
 * - APU writes at $4000-$4017 produce sound
 */

#include "nsf_player.h"
#include "apu_if.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Memory write handler: intercept APU register writes */
static void cpu_write_handler(uint16_t addr, uint8_t data, void *userdata)
{
    nsf_player_t *player = (nsf_player_t *)userdata;

    /* APU registers */
    if (addr >= 0x4000 && addr <= 0x4017 && addr != 0x4014 && addr != 0x4016) {
        apuif_write_reg(addr, data);
        /* FNV-1a over (register, value): the frame's fingerprint for the
         * loop finder. */
        uint32_t h = player->end.frame_hash;
        h = (h ^ (uint8_t)addr) * 16777619u;
        h = (h ^ data) * 16777619u;
        player->end.frame_hash = h;
    }

    /* Bankswitch registers ($5FF8-$5FFF) */
    if (player->use_bankswitch && addr >= 0x5FF8 && addr <= 0x5FFF) {
        int bank = addr - 0x5FF8;
        uint32_t offset = (uint32_t)data * 0x1000;
        uint16_t dest = 0x8000 + bank * 0x1000;

        if (offset < player->rom_size) {
            uint32_t copy_size = 0x1000;
            if (offset + copy_size > player->rom_size) {
                copy_size = player->rom_size - offset;
            }
            memcpy(&player->cpu.mem[dest], &player->rom_data[offset], copy_size);
        }
    }

    /* Some NSFs also use $5FF6-$5FF7 for $6000-$7FFF banking */
    if (player->use_bankswitch && addr >= 0x5FF6 && addr <= 0x5FF7) {
        int bank = addr - 0x5FF6;
        uint32_t offset = (uint32_t)data * 0x1000;
        uint16_t dest = 0x6000 + bank * 0x1000;

        if (offset < player->rom_size) {
            uint32_t copy_size = 0x1000;
            if (offset + copy_size > player->rom_size) {
                copy_size = player->rom_size - offset;
            }
            memcpy(&player->cpu.mem[dest], &player->rom_data[offset], copy_size);
        }
    }
}

static int load_nsf_data(nsf_player_t *player, const uint8_t *data, uint32_t size)
{
    if (size < 128) {
        fprintf(stderr, "NSF: File too small\n");
        return -1;
    }

    /* Parse header */
    memcpy(&player->header, data, sizeof(nsf_header_t));

    if (memcmp(player->header.tag, "NESM\x1A", 5) != 0) {
        fprintf(stderr, "NSF: Invalid header magic\n");
        return -1;
    }

    printf("NSF: '%s' by '%s'\n", player->header.song_name, player->header.artist);
    printf("NSF: %d songs, load=$%04X init=$%04X play=$%04X\n",
           player->header.total_songs,
           player->header.load_addr,
           player->header.init_addr,
           player->header.play_addr);

    /* Check for bankswitching */
    player->use_bankswitch = false;
    for (int i = 0; i < 8; i++) {
        if (player->header.bankswitch[i] != 0) {
            player->use_bankswitch = true;
            break;
        }
    }

    if (player->use_bankswitch) {
        printf("NSF: Using bankswitching\n");
    }

    if (player->header.chip_flags) {
        printf("NSF: Warning - extra chip flags=0x%02X (not supported)\n",
               player->header.chip_flags);
    }

    /* Store ROM data.
     * For bankswitched NSFs, the ROM data may start at an offset
     * within the first 4KB page (load_addr & 0x0FFF).
     * We pad the beginning so bank 0 aligns to a 4KB boundary. */
    uint32_t rom_offset = 128; /* After header */
    uint32_t raw_rom_size = size - rom_offset;
    uint32_t pad = player->header.load_addr & 0x0FFF;

    player->rom_size = raw_rom_size + pad;
    player->rom_data = (uint8_t *)apuemu_malloc(player->rom_size);
    if (!player->rom_data) {
        fprintf(stderr, "NSF: Failed to allocate ROM data\n");
        return -1;
    }
    memset(player->rom_data, 0, pad);
    memcpy(player->rom_data + pad, data + rom_offset, raw_rom_size);

    printf("NSF: ROM size=%u, pad=%u, total=%u\n", raw_rom_size, pad, player->rom_size);

    /* Initialize CPU */
    nes_cpu_init(&player->cpu);
    nes_cpu_set_write_cb(&player->cpu, cpu_write_handler, player);

    /* Load ROM into CPU memory */
    if (player->use_bankswitch) {
        /* Set up initial banks.
         * NSF bankswitch maps 8 x 4KB pages to $8000-$FFFF.
         * Each bankswitch[i] value is a 4KB bank number in the ROM. */
        for (int i = 0; i < 8; i++) {
            uint32_t offset = (uint32_t)player->header.bankswitch[i] * 0x1000;
            uint16_t dest = 0x8000 + i * 0x1000;
            if (offset < player->rom_size) {
                uint32_t copy_size = 0x1000;
                if (offset + copy_size > player->rom_size) {
                    copy_size = player->rom_size - offset;
                }
                memcpy(&player->cpu.mem[dest], &player->rom_data[offset], copy_size);
            }
        }
        printf("NSF: Banks loaded: ");
        for (int i = 0; i < 8; i++) printf("%d ", player->header.bankswitch[i]);
        printf("\n");
    } else {
        /* Non-bankswitched: load directly at load_addr */
        uint16_t load_addr = player->header.load_addr;
        uint32_t copy_size = raw_rom_size;
        if (load_addr + copy_size > 0x10000) {
            copy_size = 0x10000 - load_addr;
        }
        memcpy(&player->cpu.mem[load_addr], data + rom_offset, copy_size);
    }

    player->loaded = true;
    return 0;
}

/* ------------------------------------------------------------------ */
/* End of a song (see nsf_player.h)                                    */
/* ------------------------------------------------------------------ */

#define FNV_BASIS 2166136261u
#define ROLL_BASE 0x01000193u
#define HIST_MASK (NSF_END_HIST_FRAMES - 1)

static void end_free(nsf_end_t *e)
{
    if (e->hist) apuemu_free(e->hist);
    if (e->anchor_f) apuemu_free(e->anchor_f);
    if (e->anchor_t) apuemu_free(e->anchor_t);
    e->hist = NULL;
    e->anchor_f = NULL;
    e->anchor_t = NULL;
}

/* Back to the start of a song. The buffers are allocated on the first start
 * and kept until the player is freed; if they cannot be had, the loop finder
 * stays off and the silence and the limit still end the song. */
static void end_reset(nsf_end_t *e)
{
    if (!e->hist) {
        e->hist = (uint16_t *)apuemu_malloc(NSF_END_HIST_FRAMES * sizeof(uint16_t));
        e->anchor_f = (uint32_t *)apuemu_malloc(NSF_END_ANCHORS * sizeof(uint32_t));
        e->anchor_t = (uint16_t *)apuemu_malloc(NSF_END_ANCHORS * NSF_END_ANCHOR_DEPTH *
                                                sizeof(uint16_t));
        if (!e->hist || !e->anchor_f || !e->anchor_t) {
            fprintf(stderr, "NSF: no memory for the loop finder\n");
            end_free(e);
        }
    }
    if (e->anchor_t) {
        memset(e->anchor_f, 0, NSF_END_ANCHORS * sizeof(uint32_t));
        memset(e->anchor_t, 0, NSF_END_ANCHORS * NSF_END_ANCHOR_DEPTH * sizeof(uint16_t));
    }

    uint32_t pw = 1;
    for (int i = 0; i < NSF_END_WINDOW; i++) pw *= ROLL_BASE;
    e->roll_pow = pw;
    e->frame_hash = FNV_BASIS;
    e->roll = 0;
    e->changes = 0;
    e->frames = 0;
    e->silent_run = 0;
    e->period = 0;
    e->run = 0;
    e->miss = 0;
    e->best_period = 0;
    e->best_run = 0;
    e->fade_left = 0;
    e->reason = NSF_END_NONE;
    e->done = false;
}

/* A loop shorter than NSF_END_MIN_LOOP_FRAMES is counted in whole repeats
 * that reach it: a two-second figure repeating is not yet a song looping. */
static uint32_t end_lap_frames(uint32_t period)
{
    uint32_t n = (NSF_END_MIN_LOOP_FRAMES + period - 1) / period;
    return n * period;
}

/* Frame t against frame t - p. A song's driver may keep its tempo in a
 * fraction of a frame, so on the next lap a note can come a frame early or
 * late (and the exact repeat only every few laps): a frame matches when it
 * equals the frame a period back or one of its two neighbours. A mismatch
 * costs NSF_END_MISS_COST and a frame where something happened that does
 * match pays one back; frames where nothing changed on either side say
 * nothing either way (a driver that writes only on a new note would
 * otherwise match at any period). False once the misses pass
 * NSF_END_MISS_LIMIT. */
static bool end_miss_step(const uint16_t *hist, uint32_t t, uint32_t p, uint16_t *miss)
{
    uint16_t a = hist[t & HIST_MASK];
    uint16_t b = hist[(t - p) & HIST_MASK];
    uint16_t b0 = hist[(t - p - 1) & HIST_MASK];
    if (a != b && a != b0 && a != hist[(t - p + 1) & HIST_MASK]) {
        *miss += NSF_END_MISS_COST;
        return *miss <= NSF_END_MISS_LIMIT;
    }
    bool moved = a != hist[(t - 1) & HIST_MASK] || b != b0;
    if (moved && *miss) (*miss)--;
    return true;
}

/* How long the song has been repeating at period p, counted back from frame
 * t within the frames the ring still holds. */
static uint32_t end_backscan(const uint16_t *hist, uint32_t t, uint32_t p)
{
    uint32_t r = 0;
    uint32_t s = t;
    uint32_t lim = NSF_END_HIST_FRAMES - p - 2;
    uint16_t miss = 0;
    while (r < lim && s > p + 1 && end_miss_step(hist, s, p, &miss)) {
        r++;
        s--;
    }
    return r;
}

/* Anchor stamps: the frame number's low 15 bits with the top bit set, so 0
 * is an empty stamp. The ring is shorter than 2^15 frames, so the age of a
 * stamp still in it is never ambiguous. */
#define STAMP(t)      ((uint16_t)(((t) & 0x7FFF) | 0x8000))
#define STAMP_AGE(t, s) (((t) - (s)) & 0x7FFF)

/* At an anchor: each earlier place the same fingerprint was seen gives a
 * period, and the one that has been repeating longest (counted back) becomes
 * the candidate. The latest places alone are not enough: a song that plays a
 * section more than once inside its loop fills them with those repeats, so
 * the last stamp keeps the oldest place still in the ring (on the second lap,
 * the first). While a candidate is being counted only that oldest place is
 * tried, and it takes over when it has been repeating clearly longer: a
 * section's repeat must not hide the loop itself. */
static void end_anchor(nsf_end_t *e, uint32_t t, uint32_t f)
{
    uint32_t slot = (f >> 4) & (NSF_END_ANCHORS - 1);
    uint16_t *st = &e->anchor_t[slot * NSF_END_ANCHOR_DEPTH];
    const int oldest = NSF_END_ANCHOR_DEPTH - 1;

    if (e->anchor_f[slot] != f) {
        e->anchor_f[slot] = f;
        memset(st, 0, NSF_END_ANCHOR_DEPTH * sizeof(uint16_t));
    } else {
        uint32_t best_p = 0, best_r = 0;
        for (int i = e->period ? oldest : 0; i < NSF_END_ANCHOR_DEPTH; i++) {
            if (!st[i]) continue;
            uint32_t p = STAMP_AGE(t, st[i]);
            if (p == e->period) continue;
            if (p < NSF_END_WINDOW || p >= NSF_END_HIST_FRAMES - NSF_END_WINDOW) continue;
            uint32_t r = end_backscan(e->hist, t, p);
            if (r > best_r) {
                best_r = r;
                best_p = p;
            }
        }
        if (best_r >= NSF_END_WINDOW && (!e->period || best_r > e->run + NSF_END_WINDOW)) {
            e->period = best_p;
            e->run = best_r;
            e->miss = 0;
        }
    }

    /* The newest places first; the last stamp keeps the oldest one until it
     * leaves the ring, then takes the oldest of the others. */
    uint16_t dropped = st[oldest - 1];
    memmove(&st[1], &st[0], (oldest - 1) * sizeof(uint16_t));
    st[0] = STAMP(t);
    if (!st[oldest] || STAMP_AGE(t, st[oldest]) >= NSF_END_HIST_FRAMES - NSF_END_WINDOW) {
        st[oldest] = dropped ? dropped : st[0];
    }
}

/* One frame of the loop finder: store its hash, keep the candidate period
 * going or drop it, and look for a new one at an anchor. */
static void end_loop_frame(nsf_end_t *e)
{
    uint32_t t = e->frames;
    uint32_t fh = e->frame_hash;
    uint16_t h = (uint16_t)(fh ^ (fh >> 16));
    uint16_t *hist = e->hist;
    hist[t & HIST_MASK] = h;

    /* The window's fingerprint and how much happens in it */
    e->roll = e->roll * ROLL_BASE + h;
    if (t >= 1 && h != hist[(t - 1) & HIST_MASK]) e->changes++;
    if (t >= NSF_END_WINDOW) {
        uint32_t o = t - NSF_END_WINDOW;
        e->roll -= e->roll_pow * hist[o & HIST_MASK];
        if (o >= 1 && hist[o & HIST_MASK] != hist[(o - 1) & HIST_MASK]) e->changes--;
    }

    /* The candidate: one comparison a frame */
    if (e->period) {
        if (end_miss_step(hist, t, e->period, &e->miss)) {
            e->run++;
        } else {
            e->period = 0;
            e->run = 0;
        }
    }

    if (t < NSF_END_WINDOW || e->changes < NSF_END_MIN_CHANGES) return;
    /* About one frame in sixteen is an anchor, chosen by the fingerprint's
     * own bits so the same place in the song is one on every lap. */
    if ((e->roll & 15) != 0) return;
    end_anchor(e, t, e->roll);
}

void nsf_player_end_frame(nsf_player_t *player, uint32_t level)
{
    nsf_end_t *e = &player->end;
    if (!player->playing || e->done) return;

    if (e->fade_left) {
        if (--e->fade_left == 0) e->done = true;
        e->frames++;
        return;
    }

    /* Silence: the song stopped by itself, so there is nothing to fade */
    if (level <= NSF_END_SILENCE_LEVEL) {
        if (++e->silent_run >= NSF_END_SILENCE_FRAMES) {
            e->reason = NSF_END_SILENCE;
            e->done = true;
            e->frames++;
            return;
        }
    } else {
        e->silent_run = 0;
    }

    if (e->hist) {
        end_loop_frame(e);
        if (e->run > e->best_run) {
            e->best_run = e->run;
            e->best_period = e->period;
        }
    }
    e->frame_hash = FNV_BASIS;
    e->frames++;

    if (e->period && e->run >= (NSF_END_LAPS - 1) * end_lap_frames(e->period)) {
        e->reason = NSF_END_LOOP;
    } else if (e->frames >= NSF_END_HARD_LIMIT_FRAMES ||
               (e->frames >= NSF_END_LIMIT_FRAMES &&
                !(e->period && e->run >= NSF_END_MIN_LOOP_FRAMES))) {
        /* Past the limit, unless a loop is being counted: then it may finish
         * its lap, up to the hard limit. */
        e->reason = NSF_END_LIMIT;
    }
    if (e->reason != NSF_END_NONE) e->fade_left = NSF_END_FADE_FRAMES;
}

uint32_t nsf_player_end_gain_q16(const nsf_player_t *player)
{
    const nsf_end_t *e = &player->end;
    if (e->done) return 0;
    if (!e->fade_left) return 65536u;
    return (uint32_t)(((uint64_t)e->fade_left << 16) / NSF_END_FADE_FRAMES);
}

int nsf_player_load(nsf_player_t *player, const char *filename)
{
    /* A loaded player must be freed first (nsf_player_free): this forgets
     * whatever the old one held. */
    memset(player, 0, sizeof(*player));

    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "NSF: Cannot open '%s'\n", filename);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = (uint8_t *)apuemu_malloc(fsize);
    if (!data) {
        fclose(f);
        return -1;
    }

    if ((long)fread(data, 1, fsize, f) != fsize) {
        apuemu_free(data);
        fclose(f);
        return -1;
    }
    fclose(f);

    int ret = load_nsf_data(player, data, (uint32_t)fsize);
    apuemu_free(data);
    return ret;
}

int nsf_player_load_mem(nsf_player_t *player, const uint8_t *data, uint32_t size)
{
    /* A loaded player must be freed first (nsf_player_free): this forgets
     * whatever the old one held. */
    memset(player, 0, sizeof(*player));
    return load_nsf_data(player, data, size);
}

int nsf_player_start(nsf_player_t *player, int song)
{
    if (!player->loaded) return -1;
    if (song < 0 || song >= player->header.total_songs) {
        fprintf(stderr, "NSF: Invalid song %d (max %d)\n", song, player->header.total_songs - 1);
        return -1;
    }

    player->current_song = song;
    player->frames_played = 0;

    /* Clear RAM (not ROM area) */
    memset(player->cpu.mem, 0, 0x800);
    memset(&player->cpu.mem[0x6000], 0, 0x2000); /* SRAM area */

    /* Reload ROM if bankswitched */
    if (player->use_bankswitch) {
        for (int i = 0; i < 8; i++) {
            uint32_t offset = (uint32_t)player->header.bankswitch[i] * 0x1000;
            uint16_t dest = 0x8000 + i * 0x1000;
            if (offset < player->rom_size) {
                uint32_t copy_size = 0x1000;
                if (offset + copy_size > player->rom_size) {
                    copy_size = player->rom_size - offset;
                }
                memcpy(&player->cpu.mem[dest], &player->rom_data[offset], copy_size);
            }
        }
    }

    /* Reset CPU state */
    player->cpu.regs.a = (uint8_t)song;
    player->cpu.regs.x = 0; /* 0 = NTSC */
    player->cpu.regs.y = 0;
    player->cpu.regs.sp = 0xFF;
    player->cpu.regs.p = FLAG_U | FLAG_I;
    player->cpu.cycles = 0;
    player->cpu.halted = false;

    /* Enable all APU channels */
    apuif_write_reg(0x4015, 0x0F);

    /* Debug: dump first bytes at init_addr */
    printf("NSF: Code at $%04X: ", player->header.init_addr);
    for (int i = 0; i < 16; i++) {
        printf("%02X ", player->cpu.mem[player->header.init_addr + i]);
    }
    printf("\n");

    /* Call INIT routine */
    printf("NSF: Calling INIT($%04X) with song=%d\n", player->header.init_addr, song);
    nes_cpu_jsr(&player->cpu, player->header.init_addr);
    printf("NSF: INIT complete (%d cycles)\n", player->cpu.cycles);

    /* INIT's own writes are not a frame of the song */
    end_reset(&player->end);
    player->playing = true;
    return 0;
}

void nsf_player_tick(nsf_player_t *player)
{
    if (!player->playing) return;

    /* Reset cycle counter and halt flag for this frame */
    player->cpu.cycles = 0;
    player->cpu.halted = false;

    /* Call PLAY routine */
    nes_cpu_jsr(&player->cpu, player->header.play_addr);

    player->frames_played++;
}

void nsf_player_free(nsf_player_t *player)
{
    end_free(&player->end);
    if (player->rom_data) {
        apuemu_free(player->rom_data);
        player->rom_data = NULL;
    }
    player->loaded = false;
    player->playing = false;
}
