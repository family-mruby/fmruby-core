// Host tests for the shared WAV logic (components/fmrb_audio/fmrb_wav.c).
//
// The header reader and the resampler are the two places a mistake is silent:
// a wrong chunk walk plays the header as audio, and a wrong step plays the
// clip at the wrong pitch. Both are pure functions over buffers, so they run
// here under the host compiler with no firmware, no docker and no device --
// the device check (an FFT of what actually came out) then only has to
// confirm that the same numbers survive the trip.
//
// The graphics-audio copy of fmrb_wav.c is byte-identical, so this covers it
// too; keep them that way.

#include "fmrb_wav.h"

#include <math.h>

/* -std=c11 hides M_PI (it is not in the C standard). */
#define WAV_PI 3.14159265358979323846
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            printf("FAIL %s:%d: ", __func__, __LINE__);               \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
            g_fail++;                                                 \
        }                                                             \
    } while (0)

// ---- building WAV files in memory ----------------------------------------

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

// A canonical file, with `extra_chunk` optionally inserted between fmt and
// data so the chunk walk is exercised by the shapes converters really emit.
static size_t build_wav(uint8_t *out, size_t cap, uint16_t format, uint16_t channels,
                        uint32_t rate, uint16_t bits, const int16_t *pcm, uint32_t frames,
                        const char *extra_tag, uint32_t extra_len) {
    size_t pos = 0;
    uint32_t data_bytes = frames * (uint32_t)(bits / 8) * channels;
    size_t extra_total = extra_tag ? 8 + extra_len + (extra_len & 1) : 0;
    size_t riff_size = 4 + 24 + extra_total + 8 + data_bytes;
    if (cap < 12 + 24 + extra_total + 8 + data_bytes) return 0;

    memcpy(out + pos, "RIFF", 4); pos += 4;
    put_u32(out + pos, (uint32_t)riff_size); pos += 4;
    memcpy(out + pos, "WAVE", 4); pos += 4;

    memcpy(out + pos, "fmt ", 4); pos += 4;
    put_u32(out + pos, 16); pos += 4;
    put_u16(out + pos, format); pos += 2;
    put_u16(out + pos, channels); pos += 2;
    put_u32(out + pos, rate); pos += 4;
    put_u32(out + pos, rate * channels * (bits / 8)); pos += 4;
    put_u16(out + pos, (uint16_t)(channels * (bits / 8))); pos += 2;
    put_u16(out + pos, bits); pos += 2;

    if (extra_tag) {
        memcpy(out + pos, extra_tag, 4); pos += 4;
        put_u32(out + pos, extra_len); pos += 4;
        memset(out + pos, 0xAA, extra_len); pos += extra_len;
        if (extra_len & 1) out[pos++] = 0;  // pad byte, not counted in the size
    }

    memcpy(out + pos, "data", 4); pos += 4;
    put_u32(out + pos, data_bytes); pos += 4;
    if (pcm) memcpy(out + pos, pcm, data_bytes);
    else memset(out + pos, 0, data_bytes);
    pos += data_bytes;
    return pos;
}

// ---- header reading -------------------------------------------------------

static void test_parse_canonical(void) {
    static uint8_t buf[4096];
    int16_t pcm[100];
    for (int i = 0; i < 100; i++) pcm[i] = (int16_t)(i * 7);

    size_t len = build_wav(buf, sizeof(buf), 1, 1, 16000, 16, pcm, 100, NULL, 0);
    CHECK(len > 0, "could not build the file");

    fmrb_wav_info_t info;
    fmrb_wav_err_t err = fmrb_wav_parse(buf, len, &info);
    CHECK(err == FMRB_WAV_OK, "err=%d (%s)", err, fmrb_wav_strerror(err));
    CHECK(info.sample_rate == 16000, "rate=%u", info.sample_rate);
    CHECK(info.channels == 1 && info.bits == 16, "ch=%u bits=%u", info.channels, info.bits);
    CHECK(info.frames == 100, "frames=%u", info.frames);
    // The samples must be found where the parser says they are.
    const int16_t *p = (const int16_t *)(buf + info.data_offset);
    CHECK(p[0] == 0 && p[99] == 99 * 7, "data_offset points at %d/%d", p[0], p[99]);
}

static void test_parse_skips_unknown_chunks(void) {
    static uint8_t buf[4096];
    int16_t pcm[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    // An odd-length LIST is the interesting one: its pad byte is not counted
    // in the chunk size, so a walk that forgets it lands mid-tag.
    size_t len = build_wav(buf, sizeof(buf), 1, 1, 8000, 16, pcm, 8, "LIST", 13);
    fmrb_wav_info_t info;
    fmrb_wav_err_t err = fmrb_wav_parse(buf, len, &info);
    CHECK(err == FMRB_WAV_OK, "err=%d (%s)", err, fmrb_wav_strerror(err));
    CHECK(info.frames == 8, "frames=%u", info.frames);
    const int16_t *p = (const int16_t *)(buf + info.data_offset);
    CHECK(p[0] == 1 && p[7] == 8, "samples came out as %d..%d", p[0], p[7]);
}

// A file written as it was generated could not know its length, so it says
// 0xFFFFFFFF and the reader has to take what is there. OpenAI's TTS answers
// exactly like this, and rejecting it would mean the cloud voice never plays.
static void test_parse_streamed_length(void) {
    static uint8_t buf[4096];
    int16_t pcm[64];
    for (int i = 0; i < 64; i++) pcm[i] = (int16_t)(i * 100);

    size_t len = build_wav(buf, sizeof(buf), 1, 1, 24000, 16, pcm, 64, NULL, 0);
    // Overwrite the data chunk's size with the streaming sentinel.
    put_u32(buf + len - 64 * 2 - 4, 0xFFFFFFFFu);

    fmrb_wav_info_t info;
    fmrb_wav_err_t err = fmrb_wav_parse(buf, len, &info);
    CHECK(err == FMRB_WAV_OK, "streamed length rejected: %s", fmrb_wav_strerror(err));
    CHECK(info.sample_rate == 24000, "rate=%u", info.sample_rate);
    CHECK(info.frames == 64, "frames=%u, expected the 64 that are there", info.frames);
    const int16_t *p = (const int16_t *)(buf + info.data_offset);
    CHECK(p[0] == 0 && p[63] == 6300, "samples came out as %d..%d", p[0], p[63]);

    // The same clamp covers a download that stopped early: play what arrived.
    len = build_wav(buf, sizeof(buf), 1, 1, 16000, 16, pcm, 64, NULL, 0);
    err = fmrb_wav_parse(buf, len - 40, &info);
    CHECK(err == FMRB_WAV_OK, "short file rejected: %s", fmrb_wav_strerror(err));
    CHECK(info.frames == 44, "frames=%u, expected the 44 that survived", info.frames);
}

static void test_parse_rejections(void) {
    static uint8_t buf[4096];
    int16_t pcm[8] = {0};
    fmrb_wav_info_t info;

    size_t len = build_wav(buf, sizeof(buf), 1, 2, 16000, 16, pcm, 4, NULL, 0);
    CHECK(fmrb_wav_parse(buf, len, &info) == FMRB_WAV_ERR_UNSUPPORTED, "stereo accepted");

    len = build_wav(buf, sizeof(buf), 1, 1, 16000, 8, pcm, 8, NULL, 0);
    CHECK(fmrb_wav_parse(buf, len, &info) == FMRB_WAV_ERR_UNSUPPORTED, "8-bit accepted");

    len = build_wav(buf, sizeof(buf), 0xFFFE, 1, 16000, 16, pcm, 8, NULL, 0);
    CHECK(fmrb_wav_parse(buf, len, &info) == FMRB_WAV_ERR_UNSUPPORTED, "extensible accepted");

    len = build_wav(buf, sizeof(buf), 1, 1, 4000, 16, pcm, 8, NULL, 0);
    CHECK(fmrb_wav_parse(buf, len, &info) == FMRB_WAV_ERR_UNSUPPORTED, "4 kHz accepted");

    len = build_wav(buf, sizeof(buf), 1, 1, 96000, 16, pcm, 8, NULL, 0);
    CHECK(fmrb_wav_parse(buf, len, &info) == FMRB_WAV_ERR_UNSUPPORTED, "96 kHz accepted");

    len = build_wav(buf, sizeof(buf), 1, 1, 16000, 16, pcm, 0, NULL, 0);
    CHECK(fmrb_wav_parse(buf, len, &info) == FMRB_WAV_ERR_EMPTY, "empty data accepted");

    // Not a WAV at all, and a WAV cut off mid-header.
    memcpy(buf, "NOPE", 4);
    CHECK(fmrb_wav_parse(buf, 64, &info) == FMRB_WAV_ERR_FORMAT, "junk accepted");
    // A file cut inside its header is still refused; one cut inside the data
    // is not (see test_parse_streamed_length).
    len = build_wav(buf, sizeof(buf), 1, 1, 16000, 16, pcm, 8, "LIST", 40);
    CHECK(fmrb_wav_parse(buf, len - 60, &info) == FMRB_WAV_ERR_FORMAT, "truncated accepted");
    CHECK(fmrb_wav_parse(buf, 8, &info) == FMRB_WAV_ERR_FORMAT, "8-byte file accepted");
}

// ---- resampling -----------------------------------------------------------

// The whole point of the resampler: a tone keeps its pitch. Synthesise at
// src_rate, resample to out_rate, and measure the output frequency by
// counting zero crossings -- the same property fmrb_audio_probe measures with
// an FFT on the device, done here with arithmetic.
static void check_pitch_survives(uint32_t src_rate, uint32_t out_rate, double freq) {
    uint32_t frames = src_rate;  // one second
    int16_t *pcm = (int16_t *)malloc(frames * sizeof(int16_t));
    for (uint32_t i = 0; i < frames; i++) {
        pcm[i] = (int16_t)(sin(2.0 * WAV_PI * freq * i / src_rate) * 20000.0);
    }

    fmrb_wav_stream_t st;
    fmrb_wav_stream_start(&st, pcm, frames, src_rate, out_rate);
    CHECK(st.playing, "stream did not start");

    int out_n = (int)out_rate;
    int16_t *out = (int16_t *)calloc((size_t)out_n, sizeof(int16_t));
    int mixed = fmrb_wav_stream_mix(&st, out, out_n);

    // src_rate/out_rate of a second of source is all there is, so the mixer
    // runs out early when playing back slower than it recorded.
    int expect = (int)((double)frames * out_rate / src_rate);
    if (expect > out_n) expect = out_n;
    CHECK(abs(mixed - expect) <= 1, "mixed %d samples, expected about %d", mixed, expect);

    int crossings = 0;
    for (int i = 1; i < mixed; i++) {
        if ((out[i - 1] < 0 && out[i] >= 0) || (out[i - 1] >= 0 && out[i] < 0)) crossings++;
    }
    double seconds = (double)mixed / out_rate;
    double measured = crossings / 2.0 / seconds;
    CHECK(fabs(measured - freq) < 2.0,
          "%u -> %u Hz: %.1f Hz came out as %.1f Hz", src_rate, out_rate, freq, measured);

    free(pcm);
    free(out);
}

static void test_resample_keeps_pitch(void) {
    check_pitch_survives(16000, 15720, 440.0);  // the shipped 16 kHz tone
    check_pitch_survives(8000, 15720, 440.0);   // and the 8 kHz one
    check_pitch_survives(48000, 15720, 1000.0); // the top of the accepted range
    check_pitch_survives(15720, 15720, 440.0);  // already at rate: no conversion
}

static void test_mix_saturates(void) {
    int16_t pcm[64];
    for (int i = 0; i < 64; i++) pcm[i] = 30000;

    fmrb_wav_stream_t st;
    fmrb_wav_stream_start(&st, pcm, 64, 15720, 15720);

    int16_t out[64];
    for (int i = 0; i < 64; i++) out[i] = 30000;  // as if the APU were already loud
    fmrb_wav_stream_mix(&st, out, 64);
    for (int i = 0; i < 64; i++) {
        CHECK(out[i] == 32767, "sample %d clipped to %d instead of the ceiling", i, out[i]);
    }

    // And the other rail.
    for (int i = 0; i < 64; i++) pcm[i] = -30000;
    fmrb_wav_stream_start(&st, pcm, 64, 15720, 15720);
    for (int i = 0; i < 64; i++) out[i] = -30000;
    fmrb_wav_stream_mix(&st, out, 64);
    for (int i = 0; i < 64; i++) {
        CHECK(out[i] == -32768, "sample %d clipped to %d instead of the floor", i, out[i]);
    }
}

static void test_mix_adds_rather_than_replaces(void) {
    int16_t pcm[4] = {100, 100, 100, 100};
    fmrb_wav_stream_t st;
    fmrb_wav_stream_start(&st, pcm, 4, 15720, 15720);

    int16_t out[4] = {-100, 0, 50, 7};
    fmrb_wav_stream_mix(&st, out, 4);
    CHECK(out[0] == 0 && out[1] == 100 && out[2] == 150 && out[3] == 107,
          "got %d %d %d %d", out[0], out[1], out[2], out[3]);
}

static void test_stream_ends_and_stays_quiet(void) {
    int16_t pcm[4] = {1000, 1000, 1000, 1000};
    fmrb_wav_stream_t st;
    fmrb_wav_stream_start(&st, pcm, 4, 15720, 15720);

    int16_t out[16] = {0};
    int mixed = fmrb_wav_stream_mix(&st, out, 16);
    CHECK(mixed == 4, "mixed %d of 4 samples", mixed);
    CHECK(!st.playing, "stream still playing after it ran out");
    for (int i = 4; i < 16; i++) CHECK(out[i] == 0, "sample %d is %d, not silence", i, out[i]);

    // A stopped stream must keep adding nothing, however often it is asked.
    memset(out, 0, sizeof(out));
    CHECK(fmrb_wav_stream_mix(&st, out, 16) == 0, "a finished stream mixed something");
    for (int i = 0; i < 16; i++) CHECK(out[i] == 0, "sample %d is %d after the end", i, out[i]);
}

static void test_stop_is_safe(void) {
    int16_t pcm[4] = {1000, 1000, 1000, 1000};
    fmrb_wav_stream_t st;
    fmrb_wav_stream_start(&st, pcm, 4, 15720, 15720);
    fmrb_wav_stream_stop(&st);

    int16_t out[4] = {0};
    CHECK(fmrb_wav_stream_mix(&st, out, 4) == 0, "a stopped stream mixed something");
    CHECK(out[0] == 0, "a stopped stream wrote %d", out[0]);

    // Degenerate starts leave the stream stopped rather than dividing by zero.
    fmrb_wav_stream_start(&st, pcm, 4, 0, 15720);
    CHECK(!st.playing, "started with a zero source rate");
    fmrb_wav_stream_start(&st, pcm, 4, 16000, 0);
    CHECK(!st.playing, "started with a zero output rate");
    fmrb_wav_stream_start(&st, NULL, 4, 16000, 15720);
    CHECK(!st.playing, "started with no samples");
    fmrb_wav_stream_start(&st, pcm, 0, 16000, 15720);
    CHECK(!st.playing, "started with zero frames");
}

// ---- the shipped files ----------------------------------------------------

// tool/gen_sine_wav.rb writes these; if what it produces stops being what
// play_wav accepts, that is a break the device check would find much later.
static void test_shipped_tones(const char *dir) {
    const char *names[] = {"sine440_16k.wav", "sine440_8k.wav"};
    const uint32_t rates[] = {16000, 8000};

    for (int i = 0; i < 2; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        FILE *fp = fopen(path, "rb");
        if (!fp) {
            printf("FAIL %s: cannot open %s\n", __func__, path);
            g_fail++;
            continue;
        }
        fseek(fp, 0, SEEK_END);
        long len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        uint8_t *buf = (uint8_t *)malloc((size_t)len);
        size_t rd = fread(buf, 1, (size_t)len, fp);
        fclose(fp);
        CHECK(rd == (size_t)len, "%s: short read", names[i]);

        fmrb_wav_info_t info;
        fmrb_wav_err_t err = fmrb_wav_parse(buf, (size_t)len, &info);
        CHECK(err == FMRB_WAV_OK, "%s: %s", names[i], fmrb_wav_strerror(err));
        CHECK(info.sample_rate == rates[i], "%s: rate=%u", names[i], info.sample_rate);
        CHECK(info.frames == rates[i], "%s: %u frames, expected one second",
              names[i], info.frames);
        free(buf);
    }
}

int main(int argc, char **argv) {
    test_parse_canonical();
    test_parse_skips_unknown_chunks();
    test_parse_streamed_length();
    test_parse_rejections();
    test_resample_keeps_pitch();
    test_mix_saturates();
    test_mix_adds_rather_than_replaces();
    test_stream_ends_and_stays_quiet();
    test_stop_is_safe();
    if (argc > 1) test_shipped_tones(argv[1]);

    if (g_fail) {
        printf("WAV tests: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("WAV tests: all passed\n");
    return 0;
}
