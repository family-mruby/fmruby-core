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
//
// IMA-ADPCM (the bundled voices, doc/flash_layout/report/f3.md): a block built
// by hand checks the nibble arithmetic, and the shipped voices are decoded
// through the stream and compared sample for sample with what
// tools/ima_adpcm.rb decodes (that decoder was checked once against ffmpeg's;
// rakelib/test.rake writes its output next to the runner).

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

// ---- IMA-ADPCM --------------------------------------------------------------

// A mono IMA-ADPCM file: fmt (20 bytes, with the samples per block), an
// optional fact, then `nblocks` blocks of `ba` bytes copied from `blocks`.
static size_t build_adpcm(uint8_t *out, size_t cap, uint32_t rate, uint16_t ba,
                          const uint8_t *blocks, uint32_t data_bytes, uint32_t fact) {
    size_t need = 12 + 28 + (fact ? 12 : 0) + 8 + data_bytes;
    if (cap < need) return 0;
    size_t pos = 0;
    memcpy(out, "RIFF", 4); put_u32(out + 4, (uint32_t)(need - 8));
    memcpy(out + 8, "WAVE", 4); pos = 12;
    memcpy(out + pos, "fmt ", 4); put_u32(out + pos + 4, 20); pos += 8;
    put_u16(out + pos, 0x11); put_u16(out + pos + 2, 1);
    put_u32(out + pos + 4, rate); put_u32(out + pos + 8, rate / 2);
    put_u16(out + pos + 12, ba); put_u16(out + pos + 14, 4);
    put_u16(out + pos + 16, 2); put_u16(out + pos + 18, (uint16_t)((ba - 4) * 2 + 1));
    pos += 20;
    if (fact) {
        memcpy(out + pos, "fact", 4); put_u32(out + pos + 4, 4); put_u32(out + pos + 8, fact);
        pos += 12;
    }
    memcpy(out + pos, "data", 4); put_u32(out + pos + 4, data_bytes); pos += 8;
    memcpy(out + pos, blocks, data_bytes);
    return pos + data_bytes;
}

// Play a parsed file at its own rate (one source frame per output sample),
// so what comes out is the decoded clip itself.
static int play_at_own_rate(uint8_t *buf, const fmrb_wav_info_t *info, int16_t *out, int cap) {
    fmrb_wav_stream_t st;
    fmrb_wav_err_t err = fmrb_wav_stream_start_file(&st, buf, info, info->sample_rate);
    CHECK(err == FMRB_WAV_OK, "start_file: %s", fmrb_wav_strerror(err));
    memset(out, 0, (size_t)cap * sizeof(int16_t));
    int n = fmrb_wav_stream_mix(&st, out, cap);
    CHECK(!st.playing || n < cap, "the stream kept playing past its end");
    return n;
}

static void test_adpcm_by_hand(void) {
    // Two blocks of 8 bytes (9 samples each). Block 0 starts at 1000 with
    // step index 0; its nibbles are 7, 7, 8+7 (down), 0, then zeros.
    // By hand, with diff = ((2n + 1) * step) >> 3:
    //   7: step 7  -> diff 13, 1013, index 0 + 8 = 8
    //   7: step 16 -> diff 30, 1043, index 16
    //  15: step 34 -> diff 63,  980, index 24
    //   0: step 73 -> diff 9,   989, index 23
    //   0: step 66 -> diff 8,   997, index 22
    //   0: step 60 -> diff 7,  1004, index 21
    //   0: step 55 -> diff 6,  1010, index 20
    //   0: step 50 -> diff 6,  1016, index 19
    // Block 1 restarts at -500 with the index its header gives (88 clamps
    // a junk 200) and the fact chunk stops the clip after its 3rd sample.
    static const uint8_t blocks[16] = {
        0xE8, 0x03, 0, 0, 0x77, 0x0F, 0x00, 0x00,
        0x0C, 0xFE, 200, 0, 0x01, 0x00, 0x00, 0x00,
    };
    static const int16_t expect[12] = {1000, 1013, 1043, 980, 989, 997, 1004, 1010, 1016,
                                       -500, 0, 0};
    static uint8_t buf[256];
    size_t len = build_adpcm(buf, sizeof(buf), 16000, 8, blocks, 16, 12);
    fmrb_wav_info_t info;
    fmrb_wav_err_t err = fmrb_wav_parse(buf, len, &info);
    CHECK(err == FMRB_WAV_OK, "err=%s", fmrb_wav_strerror(err));
    CHECK(info.format == FMRB_WAV_FORMAT_IMA_ADPCM && info.frames == 12,
          "format=0x%x frames=%u", info.format, info.frames);
    int16_t out[32];
    int n = play_at_own_rate(buf, &info, out, 32);
    CHECK(n == 12, "played %d samples, expected 12", n);
    for (int i = 0; i < 10 && i < n; i++) {
        CHECK(out[i] == expect[i], "sample %d is %d, expected %d", i, out[i], expect[i]);
    }
    // Block 1, nibble 1 from index 88: step 32767 -> diff 12287, -500 + 12287.
    CHECK(n > 10 && out[10] == 11787, "sample 10 is %d, expected 11787", out[10]);

    // Without the fact chunk both whole blocks play (18 samples), and a block
    // cut short plays the samples it has: 5 bytes of block 1 = 3 samples.
    len = build_adpcm(buf, sizeof(buf), 16000, 8, blocks, 16, 0);
    CHECK(fmrb_wav_parse(buf, len, &info) == FMRB_WAV_OK && info.frames == 18, "frames=%u", info.frames);
    len = build_adpcm(buf, sizeof(buf), 16000, 8, blocks, 13, 0);
    CHECK(fmrb_wav_parse(buf, len, &info) == FMRB_WAV_OK && info.frames == 12, "frames=%u", info.frames);

    // A samples-per-block that disagrees with the block size, and 3-bit
    // ADPCM, are refused rather than played as noise.
    len = build_adpcm(buf, sizeof(buf), 16000, 8, blocks, 16, 0);
    put_u16(buf + 12 + 8 + 18, 10);
    CHECK(fmrb_wav_parse(buf, len, &info) == FMRB_WAV_ERR_UNSUPPORTED, "wrong samples per block accepted");
    len = build_adpcm(buf, sizeof(buf), 16000, 8, blocks, 16, 0);
    put_u16(buf + 12 + 8 + 14, 3);
    CHECK(fmrb_wav_parse(buf, len, &info) == FMRB_WAV_ERR_UNSUPPORTED, "3-bit ADPCM accepted");
}

// A clip longer than 65536 frames (4 s at 16 kHz) plays to its end: the read
// position used to be one 16.16 word, which wrapped there and started over.
static void test_long_clip_does_not_wrap(void) {
    uint32_t frames = 100000;
    int16_t *pcm = (int16_t *)malloc(frames * sizeof(int16_t));
    for (uint32_t i = 0; i < frames; i++) pcm[i] = (int16_t)(i & 0x3FFF);
    fmrb_wav_stream_t st;
    fmrb_wav_stream_start(&st, pcm, frames, 16000, 16000);
    int16_t *out = (int16_t *)calloc(frames + 16, sizeof(int16_t));
    int n = fmrb_wav_stream_mix(&st, out, (int)frames + 16);
    CHECK(n == (int)frames, "mixed %d of %u frames", n, frames);
    CHECK(out[70000] == (int16_t)(70000 & 0x3FFF), "frame 70000 came out as %d", out[70000]);
    free(pcm);
    free(out);
}

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)n + 1);
    size_t rd = fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    *len = rd;
    return buf;
}

// The bundled voices: IMA-ADPCM at 16 kHz, decoded through the stream exactly
// as tools/ima_adpcm.rb decodes them (ref_dir/<name>.raw, 16-bit LE).
static void test_shipped_voices(const char *dir, const char *ref_dir) {
    const char *names[] = {"riichi", "pon", "chi", "kan", "tsumo", "ron"};
    for (int i = 0; i < 6; i++) {
        char path[512];
        size_t len = 0, rlen = 0;
        snprintf(path, sizeof(path), "%s/%s.wav", dir, names[i]);
        uint8_t *buf = read_file(path, &len);
        snprintf(path, sizeof(path), "%s/%s.raw", ref_dir, names[i]);
        uint8_t *ref = read_file(path, &rlen);
        if (!buf || !ref) {
            printf("FAIL %s: cannot open %s or its reference\n", __func__, names[i]);
            g_fail++;
            free(buf);
            free(ref);
            continue;
        }
        uint8_t *again = (uint8_t *)malloc(len);
        memcpy(again, buf, len);
        fmrb_wav_info_t info;
        fmrb_wav_err_t err = fmrb_wav_parse(buf, len, &info);
        CHECK(err == FMRB_WAV_OK, "%s: %s", names[i], fmrb_wav_strerror(err));
        CHECK(info.format == FMRB_WAV_FORMAT_IMA_ADPCM && info.sample_rate == 16000,
              "%s: format 0x%x at %u Hz", names[i], info.format, info.sample_rate);
        CHECK(info.frames == rlen / 2, "%s: %u frames, the reference has %zu", names[i], info.frames, rlen / 2);
        int cap = (int)info.frames + 64;
        int16_t *out = (int16_t *)malloc((size_t)cap * sizeof(int16_t));
        int n = play_at_own_rate(buf, &info, out, cap);
        CHECK(n == (int)info.frames, "%s: played %d of %u", names[i], n, info.frames);
        int bad = 0, first = -1, peak = 0;
        for (int k = 0; k < n && (size_t)k < rlen / 2; k++) {
            int16_t r = (int16_t)(ref[2 * k] | (ref[2 * k + 1] << 8));
            if (out[k] != r) { bad++; if (first < 0) first = k; }
            if (abs(out[k]) > peak) peak = abs(out[k]);
        }
        CHECK(bad == 0, "%s: %d samples differ from the reference, first at %d", names[i], bad, first);
        // Each word was scaled to a -1 dBFS peak before it was encoded.
        CHECK(peak > 20000, "%s: peak %d, too quiet for a -1 dBFS word", names[i], peak);
        // At the APU's rate (the decoder held while the resampler steps
        // between its samples) the clip lasts as long as it did.
        // (start_file wrote the decoder over the header: start from a fresh copy.)
        memcpy(buf, again, len);
        CHECK(fmrb_wav_parse(buf, len, &info) == FMRB_WAV_OK, "%s: copy unreadable", names[i]);
        fmrb_wav_stream_t st;
        fmrb_wav_stream_start_file(&st, buf, &info, 15720);
        memset(out, 0, (size_t)cap * sizeof(int16_t));
        n = fmrb_wav_stream_mix(&st, out, cap);
        int expect = (int)((double)info.frames * 15720 / 16000);
        CHECK(abs(n - expect) <= 1, "%s: %d samples at 15720 Hz, expected about %d", names[i], n, expect);
        free(out);
        free(again);
        free(buf);
        free(ref);
    }
}

// ---- the shipped files ----------------------------------------------------

// tools/gen_sine_wav.rb writes these; if what it produces stops being what
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
    test_adpcm_by_hand();
    test_long_clip_does_not_wrap();
    if (argc > 1) test_shipped_tones(argv[1]);
    if (argc > 3) test_shipped_voices(argv[2], argv[3]);

    if (g_fail) {
        printf("WAV tests: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("WAV tests: all passed\n");
    return 0;
}
