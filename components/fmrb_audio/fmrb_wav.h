// WAV parsing and resampling for play_wav, shared by both audio backends.
//
// The Modern firmware mixes PCM in main/drivers/audio_p4 and the simulator /
// Retro machines mix it in fmruby-graphics-audio, so this file is duplicated
// there (main/common/fmrb_wav.h + main/audio/fmrb_wav.c) the same way
// audio_commands.h is. Keep the copies identical apart from that note: a
// resampler that disagrees between the two would change the pitch depending
// on which machine played the file.
//
// What is NOT here, on purpose: opening the file and allocating the samples.
// The two sides prefix paths differently ("/flash" vs "flash") and allocate
// from different heaps, so each owns its loader and hands the bytes here.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What play_wav accepts. Anything else is refused with one log line rather
 * than half-played: a stereo or 8-bit file read as mono 16-bit is noise.
 * Two encodings: plain PCM (16-bit) and IMA-ADPCM (format 0x11, 4-bit, the
 * blocks ffmpeg's adpcm_ima_wav writes), which is how the bundled voices
 * ship (doc/flash_layout/report/f3.md, tools/ima_adpcm.rb). */
#define FMRB_WAV_MIN_RATE      8000u
#define FMRB_WAV_MAX_RATE      48000u
#define FMRB_WAV_MAX_BYTES     (2u * 1024u * 1024u)

#define FMRB_WAV_FORMAT_PCM        0x0001u
#define FMRB_WAV_FORMAT_IMA_ADPCM  0x0011u

typedef enum {
    FMRB_WAV_OK = 0,
    FMRB_WAV_ERR_FORMAT,       /* not a RIFF/WAVE file, or a chunk runs off the end */
    FMRB_WAV_ERR_UNSUPPORTED,  /* readable, but not PCM 16-bit / IMA-ADPCM 4-bit mono in range */
    FMRB_WAV_ERR_EMPTY         /* no data chunk, or no whole sample in it */
} fmrb_wav_err_t;

typedef struct {
    uint32_t sample_rate;
    uint32_t data_offset;  /* byte offset of the data chunk's body within the file */
    uint32_t data_bytes;   /* bytes of it that are in the file */
    uint32_t frames;       /* mono samples the clip plays (ADPCM: decoded samples) */
    uint16_t channels;
    uint16_t bits;
    uint16_t format;       /* FMRB_WAV_FORMAT_* */
    uint16_t block_align;  /* ADPCM: bytes per block (header + nibbles) */
} fmrb_wav_info_t;

/* Read the header of a whole WAV file held in `buf`. Unknown chunks between
 * fmt and data are skipped, which is what the odd LIST/fact chunk a converter
 * leaves behind needs. An ADPCM file's fact chunk, when it has one, caps
 * `frames` (the encoder fills the last block with silence). */
fmrb_wav_err_t fmrb_wav_parse(const uint8_t *buf, size_t len, fmrb_wav_info_t *out);

const char *fmrb_wav_strerror(fmrb_wav_err_t err);

/* One playing clip. The samples are not owned here: the caller keeps the
 * buffer alive until it stops the stream. The read position is a whole frame
 * index plus a 16-bit fraction (a single 16.16 word would wrap after 65536
 * frames, about 4 s at 16 kHz). Kept the size it was before ADPCM: the
 * decoder's state lives in the caller's buffer (fmrb_wav_stream_start_file),
 * so no static grows on any board. */
typedef struct {
    const void *src;    /* int16_t PCM, or the ADPCM decoder (`adpcm` says) */
    uint32_t frames;
    uint32_t pos;       /* source frame being played */
    uint32_t step_q16;  /* source frames consumed per output sample */
    uint16_t frac_q16;  /* fraction of the way to pos + 1 */
    uint8_t playing;
    uint8_t adpcm;
} fmrb_wav_stream_t;

/* Point a stream at `frames` mono PCM samples recorded at src_rate, to be
 * played out at out_rate. Rates are checked by fmrb_wav_parse; passing 0 for
 * either leaves the stream stopped rather than dividing by zero. */
void fmrb_wav_stream_start(fmrb_wav_stream_t *st, const int16_t *pcm, uint32_t frames,
                           uint32_t src_rate, uint32_t out_rate);

/* Start a stream on a whole file read into `buf` and parsed into `info`.
 * The buffer is rearranged in place and then belongs to the stream until it
 * is stopped: PCM samples are moved to its front (aligned for int16 reads);
 * an ADPCM clip keeps its blocks where they are and the decoder's state goes
 * over the header in front of them. The blocks are decoded one sample at a
 * time as the clip plays, so nothing the size of the decoded clip is ever
 * allocated. `buf` must be aligned for a pointer (any malloc is). Returns
 * FMRB_WAV_ERR_FORMAT, with the stream stopped, when the header is too short
 * to hold the decoder's state (no file a converter writes is). */
fmrb_wav_err_t fmrb_wav_stream_start_file(fmrb_wav_stream_t *st, uint8_t *buf,
                                          const fmrb_wav_info_t *info, uint32_t out_rate);

void fmrb_wav_stream_stop(fmrb_wav_stream_t *st);

/* Add `count` samples of the clip into out[], saturating instead of wrapping
 * (an int16 that wraps is a click, and the APU may already be near the rail).
 * Returns the number of samples that had clip in them; the stream stops
 * itself once it runs out, so a caller can notice by testing st->playing. */
int fmrb_wav_stream_mix(fmrb_wav_stream_t *st, int16_t *out, int count);

#ifdef __cplusplus
}
#endif
