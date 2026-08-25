// See fmrb_wav.h. Duplicated in fmruby-graphics-audio (main/audio/fmrb_wav.c);
// keep the two identical.

#include "fmrb_wav.h"

#include <string.h>

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int tag_is(const uint8_t *p, const char *tag) {
    return memcmp(p, tag, 4) == 0;
}

fmrb_wav_err_t fmrb_wav_parse(const uint8_t *buf, size_t len, fmrb_wav_info_t *out) {
    if (!buf || !out || len < 12) return FMRB_WAV_ERR_FORMAT;
    if (!tag_is(buf, "RIFF") || !tag_is(buf + 8, "WAVE")) return FMRB_WAV_ERR_FORMAT;

    memset(out, 0, sizeof(*out));

    int have_fmt = 0;
    /* The RIFF size field is not trusted for bounds: a truncated download
     * still says how long the file was meant to be. Walk within `len`. */
    size_t pos = 12;
    while (pos + 8 <= len) {
        const uint8_t *hdr = buf + pos;
        uint32_t csize = rd_u32(hdr + 4);
        size_t body = pos + 8;
        if (csize > len - body) return FMRB_WAV_ERR_FORMAT;

        if (tag_is(hdr, "fmt ")) {
            if (csize < 16) return FMRB_WAV_ERR_FORMAT;
            uint16_t format = rd_u16(buf + body);
            out->channels = rd_u16(buf + body + 2);
            out->sample_rate = rd_u32(buf + body + 4);
            out->bits = rd_u16(buf + body + 14);
            /* Format 1 is plain PCM. WAVE_FORMAT_EXTENSIBLE (0xFFFE) can also
             * be PCM, but only its sub-format GUID says so; refuse it rather
             * than guess, since guessing wrong plays noise at full volume. */
            if (format != 1) return FMRB_WAV_ERR_UNSUPPORTED;
            have_fmt = 1;
        } else if (tag_is(hdr, "data")) {
            if (!have_fmt) return FMRB_WAV_ERR_FORMAT;
            if (out->channels != 1 || out->bits != 16) return FMRB_WAV_ERR_UNSUPPORTED;
            if (out->sample_rate < FMRB_WAV_MIN_RATE ||
                out->sample_rate > FMRB_WAV_MAX_RATE) return FMRB_WAV_ERR_UNSUPPORTED;
            out->data_offset = (uint32_t)body;
            out->frames = csize / 2u;
            return out->frames ? FMRB_WAV_OK : FMRB_WAV_ERR_EMPTY;
        }

        /* Chunks are padded to an even length; the pad byte is not counted. */
        pos = body + csize + (csize & 1u);
    }
    return have_fmt ? FMRB_WAV_ERR_EMPTY : FMRB_WAV_ERR_FORMAT;
}

const char *fmrb_wav_strerror(fmrb_wav_err_t err) {
    switch (err) {
        case FMRB_WAV_OK:              return "ok";
        case FMRB_WAV_ERR_FORMAT:      return "not a WAV file";
        case FMRB_WAV_ERR_UNSUPPORTED: return "not PCM 16-bit mono 8-48 kHz";
        case FMRB_WAV_ERR_EMPTY:       return "no samples";
    }
    return "unknown";
}

void fmrb_wav_stream_start(fmrb_wav_stream_t *st, const int16_t *pcm, uint32_t frames,
                           uint32_t src_rate, uint32_t out_rate) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
    if (!pcm || frames == 0 || src_rate == 0 || out_rate == 0) return;
    st->pcm = pcm;
    st->frames = frames;
    /* 16.16 is plenty: the ratio here is at most 48000/15720 ~ 3.05, and the
     * accumulated error over a 2 MB clip (1M frames) stays under a sample. */
    st->step_q16 = (uint32_t)(((uint64_t)src_rate << 16) / out_rate);
    st->playing = 1;
}

void fmrb_wav_stream_stop(fmrb_wav_stream_t *st) {
    if (!st) return;
    st->playing = 0;
    st->pcm = NULL;
    st->frames = 0;
}

int fmrb_wav_stream_mix(fmrb_wav_stream_t *st, int16_t *out, int count) {
    if (!st || !st->playing || !st->pcm || !out || count <= 0) return 0;

    int mixed = 0;
    for (int i = 0; i < count; i++) {
        uint32_t idx = st->pos_q16 >> 16;
        if (idx >= st->frames) {
            fmrb_wav_stream_stop(st);
            break;
        }
        int32_t a = st->pcm[idx];
        /* The last frame has nothing to lean towards, so it holds. */
        int32_t b = (idx + 1 < st->frames) ? st->pcm[idx + 1] : a;
        int32_t frac = (int32_t)(st->pos_q16 & 0xFFFFu);
        int32_t s = a + (((b - a) * frac) >> 16);

        int32_t sum = (int32_t)out[i] + s;
        if (sum > 32767) sum = 32767;
        else if (sum < -32768) sum = -32768;
        out[i] = (int16_t)sum;

        st->pos_q16 += st->step_q16;
        mixed++;
    }
    return mixed;
}
