/*
 * The NSF player's end of a song (components/apu_emu/src/nsf_player.c,
 * doc/nsf_player_v2) against synthetic frame streams: the stream of frame
 * hashes the loop finder sees and the MAIN level the silence check sees are
 * made up here, frame by frame, so each case knows exactly when the song
 * should end and why. nsf_player.c is included whole (its detector is
 * static), with the CPU and the APU stubbed out. The file is the same in
 * fmruby-graphics-audio, so this covers both backends.
 *
 * Run by `rake nsf:test` (part of `rake test`).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "nsf_player.h"
#include "apu_if.h"

/* ---- stand-ins for what the player calls ---- */
void apuif_write_reg(uint32_t address, uint8_t value) { (void)address; (void)value; }
void *apuemu_malloc(uint32_t size) { return malloc(size); }
void apuemu_free(void *ptr) { free(ptr); }
void nes_cpu_init(nes_cpu_t *cpu) { (void)cpu; }
void nes_cpu_set_write_cb(nes_cpu_t *cpu, nes_cpu_write_cb_t cb, void *u) { (void)cpu; (void)cb; (void)u; }
void nes_cpu_jsr(nes_cpu_t *cpu, uint16_t addr) { (void)cpu; (void)addr; }

#include "../../components/apu_emu/src/nsf_player.c"

static int s_fail;
static int s_pass;

#define CHECK(cond, ...) do { \
    if (cond) { s_pass++; } else { s_fail++; printf("FAIL %s:%d: ", __func__, __LINE__); \
        printf(__VA_ARGS__); printf("\n"); } } while (0)

/* A deterministic pseudo-random hash for frame i of "material" m. */
static uint32_t rnd(uint32_t m, uint32_t i)
{
    uint32_t x = m * 0x9E3779B1u ^ (i + 1) * 0x85EBCA77u;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15;
    return x;
}

typedef uint32_t (*stream_fn)(uint32_t frame, uint32_t *level);

typedef struct {
    int reason;
    uint32_t fade_at;   /* the frame the fade started (0: none) */
    uint32_t end_at;    /* the frame it was over */
    uint32_t period;
    uint32_t min_gain_seen;
} result_t;

static result_t run(stream_fn fn, uint32_t max_frames)
{
    static nsf_player_t p;
    memset(&p, 0, sizeof(p));
    end_reset(&p.end);
    p.playing = true;
    result_t r = { 0, 0, 0, 0, 65536 };
    for (uint32_t f = 0; f < max_frames && !p.end.done; f++) {
        uint32_t g = nsf_player_end_gain_q16(&p);
        if (g < r.min_gain_seen) r.min_gain_seen = g;
        uint32_t level = 20000;
        p.end.frame_hash = fn(f, &level);
        nsf_player_end_frame(&p, level);
        if (!r.fade_at && p.end.fade_left) {
            r.fade_at = f + 1;
            r.period = p.end.period;
        }
        if (p.end.done) r.end_at = f + 1;
    }
    r.reason = p.end.done ? p.end.reason : NSF_END_NONE;
    end_free(&p.end);
    return r;
}

/* ---- the songs ---- */

/* An intro of 300 frames, then a loop of 1200 frames that never changes. */
static uint32_t s_exact(uint32_t f, uint32_t *level)
{
    (void)level;
    if (f < 300) return rnd(1, f);
    return rnd(2, (f - 300) % 1200);
}

/* Plays for 10 s, then nothing at all. */
static uint32_t s_silence(uint32_t f, uint32_t *level)
{
    if (f >= 600) *level = 0;
    return f < 600 ? rnd(3, f) : 0;
}

/* Never repeats. */
static uint32_t s_endless(uint32_t f, uint32_t *level)
{
    (void)level;
    return rnd(4, f);
}

/* A loop of 1800 frames whose notes come a frame late now and then, as a
 * driver that keeps its tempo in fractions of a frame does: in each lap a
 * different stretch of 20 frames is shifted by one. */
static uint32_t s_jitter(uint32_t f, uint32_t *level)
{
    (void)level;
    uint32_t lap = f / 1800, pos = f % 1800;
    uint32_t start = 100 + (lap * 337) % 1500;
    if (pos > start && pos <= start + 20) pos--;
    return rnd(5, pos);
}

/* A driver that writes only when a note starts: most frames are the empty
 * frame, and the notes come at times that never repeat. */
static uint32_t s_sparse(uint32_t f, uint32_t *level)
{
    (void)level;
    return (rnd(6, f) % 9 == 0) ? rnd(7, f) : 0;
}

/* A two-second figure repeated for 12 seconds, then something else: a figure
 * is not a loop of the song. */
static uint32_t s_figure(uint32_t f, uint32_t *level)
{
    (void)level;
    if (f < 720) return rnd(8, f % 120);
    return rnd(9, f);
}

/* A loop of 2400 frames made of sections A A B A C (480 frames each): the
 * sections that repeat inside the loop must not hide the loop. */
static uint32_t s_sections(uint32_t f, uint32_t *level)
{
    (void)level;
    static const uint32_t sec[5] = { 10, 10, 11, 10, 12 };
    uint32_t pos = f % 2400;
    return rnd(sec[pos / 480], pos % 480);
}

/* ---- the cases ---- */

static void test_exact_loop(void)
{
    result_t r = run(s_exact, 60 * 600);
    CHECK(r.reason == NSF_END_LOOP, "reason %d", r.reason);
    CHECK(r.period == 1200, "period %u", r.period);
    /* Two laps: the intro, the loop, the loop again. A few frames early at
     * most: counting back, the misses allowed reach a little into the intro. */
    CHECK(r.fade_at >= 300 + 2400 - 10 && r.fade_at <= 300 + 2400 + 2, "fade at %u", r.fade_at);
    CHECK(r.end_at == r.fade_at + NSF_END_FADE_FRAMES, "end at %u", r.end_at);
    CHECK(r.min_gain_seen < 65536 / 50, "the fade went down to %u", r.min_gain_seen);
}

static void test_silence(void)
{
    result_t r = run(s_silence, 60 * 600);
    CHECK(r.reason == NSF_END_SILENCE, "reason %d", r.reason);
    CHECK(r.end_at == 600 + NSF_END_SILENCE_FRAMES, "end at %u", r.end_at);
    CHECK(r.fade_at == 0, "no fade after silence (fade at %u)", r.fade_at);
}

static void test_limit(void)
{
    result_t r = run(s_endless, 60 * 600);
    CHECK(r.reason == NSF_END_LIMIT, "reason %d", r.reason);
    CHECK(r.fade_at == NSF_END_LIMIT_FRAMES, "fade at %u", r.fade_at);
    CHECK(r.end_at == NSF_END_LIMIT_FRAMES + NSF_END_FADE_FRAMES, "end at %u", r.end_at);
}

static void test_jitter(void)
{
    result_t r = run(s_jitter, 60 * 600);
    CHECK(r.reason == NSF_END_LOOP, "reason %d", r.reason);
    CHECK(r.period == 1800, "period %u", r.period);
    CHECK(r.fade_at <= 2 * 1800 + 120, "fade at %u", r.fade_at);
}

static void test_sparse(void)
{
    result_t r = run(s_sparse, 60 * 600);
    CHECK(r.reason == NSF_END_LIMIT, "no loop in notes that never repeat (reason %d, period %u)",
          r.reason, r.period);
}

static void test_figure(void)
{
    result_t r = run(s_figure, 60 * 600);
    CHECK(r.reason == NSF_END_LIMIT, "a 12 s figure is not a loop (reason %d, fade at %u)",
          r.reason, r.fade_at);
}

static void test_sections(void)
{
    result_t r = run(s_sections, 60 * 600);
    CHECK(r.reason == NSF_END_LOOP, "reason %d", r.reason);
    CHECK(r.period == 2400, "period %u", r.period);
    CHECK(r.fade_at >= 4800 - 10 && r.fade_at <= 4800 + 2, "fade at %u", r.fade_at);
}

/* Without the loop finder's buffers the silence and the limit still work. */
static void test_no_memory(void)
{
    static nsf_player_t p;
    memset(&p, 0, sizeof(p));
    end_reset(&p.end);
    end_free(&p.end);   /* as if apuemu_malloc had failed */
    p.playing = true;
    uint32_t f = 0;
    for (; f < 60 * 600 && !p.end.done; f++) {
        p.end.frame_hash = rnd(2, f % 1200);
        nsf_player_end_frame(&p, 20000);
    }
    CHECK(p.end.reason == NSF_END_LIMIT, "reason %d", p.end.reason);
    CHECK(f == NSF_END_LIMIT_FRAMES + NSF_END_FADE_FRAMES, "over at %u", f);
}

int main(void)
{
    test_exact_loop();
    test_silence();
    test_limit();
    test_jitter();
    test_sparse();
    test_figure();
    test_sections();
    test_no_memory();
    printf("nsf_end: %d passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
