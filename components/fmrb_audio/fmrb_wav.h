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
 * than half-played: a stereo or 8-bit file read as mono 16-bit is noise. */
#define FMRB_WAV_MIN_RATE      8000u
#define FMRB_WAV_MAX_RATE      48000u
#define FMRB_WAV_MAX_BYTES     (2u * 1024u * 1024u)

typedef enum {
    FMRB_WAV_OK = 0,
    FMRB_WAV_ERR_FORMAT,       /* not a RIFF/WAVE file, or a chunk runs off the end */
    FMRB_WAV_ERR_UNSUPPORTED,  /* readable, but not PCM 16-bit mono in range */
    FMRB_WAV_ERR_EMPTY         /* no data chunk, or no whole sample in it */
} fmrb_wav_err_t;

typedef struct {
    uint32_t sample_rate;
    uint32_t data_offset;  /* byte offset of the sample bytes within the file */
    uint32_t frames;       /* whole mono samples in the data chunk */
    uint16_t channels;
    uint16_t bits;
} fmrb_wav_info_t;

/* Read the header of a whole WAV file held in `buf`. Unknown chunks between
 * fmt and data are skipped, which is what the odd LIST/fact chunk a converter
 * leaves behind needs. */
fmrb_wav_err_t fmrb_wav_parse(const uint8_t *buf, size_t len, fmrb_wav_info_t *out);

const char *fmrb_wav_strerror(fmrb_wav_err_t err);

/* One playing clip. The samples are not owned here: the caller keeps the
 * buffer alive until it stops the stream. */
typedef struct {
    const int16_t *pcm;
    uint32_t frames;
    uint32_t pos_q16;   /* 16.16 read position, in source frames */
    uint32_t step_q16;  /* source frames consumed per output sample */
    int playing;
} fmrb_wav_stream_t;

/* Point a stream at `frames` mono samples recorded at src_rate, to be played
 * out at out_rate. Rates are checked by fmrb_wav_parse; passing 0 for either
 * leaves the stream stopped rather than dividing by zero. */
void fmrb_wav_stream_start(fmrb_wav_stream_t *st, const int16_t *pcm, uint32_t frames,
                           uint32_t src_rate, uint32_t out_rate);

void fmrb_wav_stream_stop(fmrb_wav_stream_t *st);

/* Add `count` samples of the clip into out[], saturating instead of wrapping
 * (an int16 that wraps is a click, and the APU may already be near the rail).
 * Returns the number of samples that had clip in them; the stream stops
 * itself once it runs out, so a caller can notice by testing st->playing. */
int fmrb_wav_stream_mix(fmrb_wav_stream_t *st, int16_t *out, int count);

#ifdef __cplusplus
}
#endif
