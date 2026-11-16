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
    uint32_t fact = 0;          /* ADPCM: the samples the clip really has */
    uint16_t adpcm_spb = 0;     /* ADPCM: samples per block the fmt chunk says */
    /* The RIFF size field is not trusted for bounds: a truncated download
     * still says how long the file was meant to be. Walk within `len`. */
    size_t pos = 12;
    while (pos + 8 <= len) {
        const uint8_t *hdr = buf + pos;
        uint32_t csize = rd_u32(hdr + 4);
        size_t body = pos + 8;
        uint32_t avail = (uint32_t)(len - body);

        if (tag_is(hdr, "data")) {
            if (!have_fmt) return FMRB_WAV_ERR_FORMAT;
            if (out->channels != 1) return FMRB_WAV_ERR_UNSUPPORTED;
            if (out->sample_rate < FMRB_WAV_MIN_RATE ||
                out->sample_rate > FMRB_WAV_MAX_RATE) return FMRB_WAV_ERR_UNSUPPORTED;
            /* A file that was streamed out could not know its length when the
             * header went, and writes 0xFFFFFFFF in the data chunk (OpenAI's
             * TTS does exactly this). A download that stopped early likewise
             * has less than it promised. Either way, play what arrived. */
            if (csize > avail) csize = avail;
            out->data_offset = (uint32_t)body;
            out->data_bytes = csize;
            if (out->format == FMRB_WAV_FORMAT_PCM) {
                if (out->bits != 16) return FMRB_WAV_ERR_UNSUPPORTED;
                out->frames = csize / 2u;
            } else {
                /* IMA-ADPCM, mono: each block is a 4-byte header (the first
                 * sample, the step index, a reserved byte) and then one
                 * sample per nibble. A block cut short still plays the
                 * samples it has. */
                uint32_t ba = out->block_align;
                if (out->bits != 4 || ba < 5 || ba > 0x8000u) return FMRB_WAV_ERR_UNSUPPORTED;
                uint32_t spb = (ba - 4u) * 2u + 1u;
                if (adpcm_spb && adpcm_spb != spb) return FMRB_WAV_ERR_UNSUPPORTED;
                uint32_t rest = csize % ba;
                out->frames = (csize / ba) * spb + (rest >= 4u ? (rest - 4u) * 2u + 1u : 0u);
                if (fact && fact < out->frames) out->frames = fact;
            }
            return out->frames ? FMRB_WAV_OK : FMRB_WAV_ERR_EMPTY;
        }

        /* Every other chunk has to fit, or the walk is following garbage. */
        if (csize > avail) return FMRB_WAV_ERR_FORMAT;

        if (tag_is(hdr, "fmt ")) {
            if (csize < 16) return FMRB_WAV_ERR_FORMAT;
            out->format = rd_u16(buf + body);
            out->channels = rd_u16(buf + body + 2);
            out->sample_rate = rd_u32(buf + body + 4);
            out->block_align = rd_u16(buf + body + 12);
            out->bits = rd_u16(buf + body + 14);
            /* Format 1 is plain PCM, 0x11 IMA-ADPCM. WAVE_FORMAT_EXTENSIBLE
             * (0xFFFE) can also be PCM, but only its sub-format GUID says so;
             * refuse it rather than guess, since guessing wrong plays noise at
             * full volume. */
            if (out->format != FMRB_WAV_FORMAT_PCM &&
                out->format != FMRB_WAV_FORMAT_IMA_ADPCM) return FMRB_WAV_ERR_UNSUPPORTED;
            /* ADPCM's extra field: cbSize, then the samples per block. */
            if (out->format == FMRB_WAV_FORMAT_IMA_ADPCM && csize >= 20 &&
                rd_u16(buf + body + 16) >= 2) {
                adpcm_spb = rd_u16(buf + body + 18);
            }
            have_fmt = 1;
        } else if (tag_is(hdr, "fact") && csize >= 4) {
            fact = rd_u32(buf + body);
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
        case FMRB_WAV_ERR_UNSUPPORTED: return "not PCM 16-bit or IMA-ADPCM 4-bit mono 8-48 kHz";
        case FMRB_WAV_ERR_EMPTY:       return "no samples";
    }
    return "unknown";
}

/* ---- IMA-ADPCM ------------------------------------------------------------ */

static const int16_t k_ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};

static const int8_t k_ima_index[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

/* The decoder of one clip, placed over the file's header in the caller's
 * buffer. It walks the samples strictly in order: `cur` is sample cur_idx and
 * `next` the one after it, which is all the linear resampler needs. */
typedef struct {
    const uint8_t *blocks;
    uint32_t block_align;
    uint32_t spb;          /* samples per block */
    uint32_t blk_off;      /* byte offset of the block `next` came from */
    uint32_t k;            /* `next`'s place within that block */
    uint32_t cur_idx;
    int32_t pred;
    int32_t step_index;
    int16_t cur;
    int16_t next;
} wav_adpcm_t;

#define WAV_ADPCM_HDR_ROOM ((sizeof(wav_adpcm_t) + 7u) & ~(size_t)7u)

/* Decode the sample after the last one decoded. The caller never asks past
 * the clip's frames, and fmrb_wav_parse counted only whole samples, so the
 * bytes read are inside the data chunk. */
static int16_t adpcm_decode_next(wav_adpcm_t *d) {
    if (++d->k == d->spb) {
        d->k = 0;
        d->blk_off += d->block_align;
    }
    const uint8_t *blk = d->blocks + d->blk_off;
    if (d->k == 0) {
        d->pred = (int16_t)rd_u16(blk);
        d->step_index = blk[2] > 88 ? 88 : blk[2];
        return (int16_t)d->pred;
    }
    uint32_t j = d->k - 1u;
    uint8_t byte = blk[4u + (j >> 1)];
    uint8_t nib = (j & 1u) ? (uint8_t)(byte >> 4) : (uint8_t)(byte & 0x0Fu);
    /* ((2n + 1) * step) / 8: ffmpeg's decoder, and the same sum its encoder
     * predicts with, so the two stay in step across a block (the textbook
     * bit-by-bit sum truncates differently and drifts a few steps). */
    int32_t diff = ((2 * (int32_t)(nib & 7u) + 1) * k_ima_step[d->step_index]) >> 3;
    int32_t p = (nib & 8u) ? d->pred - diff : d->pred + diff;
    if (p > 32767) p = 32767;
    else if (p < -32768) p = -32768;
    d->pred = p;
    int32_t si = d->step_index + k_ima_index[nib & 7u];
    d->step_index = si < 0 ? 0 : (si > 88 ? 88 : si);
    return (int16_t)p;
}

/* Bring the decoder to sample idx (never backwards). */
static void adpcm_seek(wav_adpcm_t *d, uint32_t idx, uint32_t frames) {
    while (d->cur_idx < idx) {
        d->cur = d->next;
        d->cur_idx++;
        d->next = (d->cur_idx + 1u < frames) ? adpcm_decode_next(d) : d->cur;
    }
}

/* ---- streams ---------------------------------------------------------------- */

static void stream_rate(fmrb_wav_stream_t *st, uint32_t src_rate, uint32_t out_rate) {
    /* 16.16 is plenty for the step: the ratio here is at most 48000/15720
     * ~ 3.05, and the fraction is carried separately from the frame index,
     * so a long clip neither wraps nor drifts. */
    st->step_q16 = (uint32_t)(((uint64_t)src_rate << 16) / out_rate);
}

void fmrb_wav_stream_start(fmrb_wav_stream_t *st, const int16_t *pcm, uint32_t frames,
                           uint32_t src_rate, uint32_t out_rate) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
    if (!pcm || frames == 0 || src_rate == 0 || out_rate == 0) return;
    st->src = pcm;
    st->frames = frames;
    stream_rate(st, src_rate, out_rate);
    st->playing = 1;
}

fmrb_wav_err_t fmrb_wav_stream_start_file(fmrb_wav_stream_t *st, uint8_t *buf,
                                          const fmrb_wav_info_t *info, uint32_t out_rate) {
    if (!st) return FMRB_WAV_ERR_FORMAT;
    memset(st, 0, sizeof(*st));
    if (!buf || !info || info->frames == 0) return FMRB_WAV_ERR_EMPTY;

    if (info->format != FMRB_WAV_FORMAT_IMA_ADPCM) {
        /* Move the samples to the front of the allocation. The data chunk
         * starts at an even offset in every file seen so far, but "so far" is
         * not an alignment guarantee on RISC-V, and one memmove is cheaper
         * than finding out the hard way. */
        memmove(buf, buf + info->data_offset, (size_t)info->frames * 2u);
        fmrb_wav_stream_start(st, (const int16_t *)(void *)buf, info->frames,
                              info->sample_rate, out_rate);
        return FMRB_WAV_OK;
    }

    if (info->data_offset < WAV_ADPCM_HDR_ROOM || out_rate == 0 || info->sample_rate == 0) {
        return FMRB_WAV_ERR_FORMAT;
    }
    wav_adpcm_t *d = (wav_adpcm_t *)(void *)buf;
    memset(d, 0, sizeof(*d));
    d->blocks = buf + info->data_offset;
    d->block_align = info->block_align;
    d->spb = ((uint32_t)info->block_align - 4u) * 2u + 1u;
    d->k = d->spb - 1u;                 /* so the first decode opens block 0 */
    d->blk_off = 0u - d->block_align;   /* ... at offset 0 (wraps back to it) */
    d->cur = adpcm_decode_next(d);
    d->next = info->frames > 1u ? adpcm_decode_next(d) : d->cur;

    st->src = d;
    st->adpcm = 1;
    st->frames = info->frames;
    stream_rate(st, info->sample_rate, out_rate);
    st->playing = 1;
    return FMRB_WAV_OK;
}

void fmrb_wav_stream_stop(fmrb_wav_stream_t *st) {
    if (!st) return;
    st->playing = 0;
    st->src = NULL;
    st->frames = 0;
}

int fmrb_wav_stream_mix(fmrb_wav_stream_t *st, int16_t *out, int count) {
    if (!st || !st->playing || !st->src || !out || count <= 0) return 0;

    int mixed = 0;
    for (int i = 0; i < count; i++) {
        uint32_t idx = st->pos;
        if (idx >= st->frames) {
            fmrb_wav_stream_stop(st);
            break;
        }
        int32_t a, b;
        if (st->adpcm) {
            wav_adpcm_t *d = (wav_adpcm_t *)(uintptr_t)st->src;
            adpcm_seek(d, idx, st->frames);
            a = d->cur;
            b = d->next;
        } else {
            const int16_t *pcm = (const int16_t *)st->src;
            a = pcm[idx];
            /* The last frame has nothing to lean towards, so it holds. */
            b = (idx + 1 < st->frames) ? pcm[idx + 1] : a;
        }
        /* Half the fraction keeps the product inside an int32 even for a
         * rail-to-rail step (65535 * 32767 < 2^31). */
        int32_t s = a + (((b - a) * (int32_t)(st->frac_q16 >> 1)) >> 15);

        int32_t sum = (int32_t)out[i] + s;
        if (sum > 32767) sum = 32767;
        else if (sum < -32768) sum = -32768;
        out[i] = (int16_t)sum;

        uint32_t t = (uint32_t)st->frac_q16 + st->step_q16;
        st->pos += t >> 16;
        st->frac_q16 = (uint16_t)(t & 0xFFFFu);
        mixed++;
    }
    return mixed;
}
