// Internal interfaces between the audio_p4 hw / handler / task units.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "audio_p4.h"
#include "audio_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- audio_p4_hw.c ---
bool audio_p4_hw_ready(void);
void audio_p4_hw_write(const int16_t *samples, int len, int channels);
void audio_p4_hw_set_volume(uint8_t volume_0_255);

// Microphone (ES7210, doc/mic_spectrum). The RX side of the same I2S port the
// speaker uses -- the two codecs share MCLK/BCLK/WS on the board -- so the
// sample rate is the speaker's and cannot be chosen separately.
bool audio_p4_mic_available(void);
int  audio_p4_mic_sample_rate(void);
// Power the microphone up or down. Returns FMRB_OK when the codec answered.
fmrb_err_t audio_p4_mic_enable(bool on);
// Read up to max_samples mono int16 samples (the left channel of the stereo
// pair). Returns how many were read, 0 on timeout, negative on error.
int audio_p4_mic_read(int16_t *dst, int max_samples, int timeout_ms);
// Bring-up check: listen for about a second and log RMS and peak per block, so
// "the microphone produces values" can be settled from a serial capture alone.
void audio_p4_mic_selftest(void);
// The loudest frequency currently being heard, in Hz (-1 when nothing could be
// read). One 512-point window through the C FFT.
int audio_p4_mic_peak_hz(void);

// --- audio_p4_handler.c (music track slots) ---
int audio_p4_store_track(uint32_t music_id, const uint8_t *data, uint32_t size);
int audio_p4_get_track(uint32_t music_id, const uint8_t **out_data, uint32_t *out_size);

// --- audio_p4_task.c (APU engine operations) ---
// All take the internal engine lock; callable from the command path.
int  audio_p4_engine_nsf_play(const char *path, int track);
void audio_p4_engine_nsf_stop(void);
int  audio_p4_engine_fmsq_play_slot(uint32_t music_id);
int  audio_p4_engine_note_on(uint8_t channel, uint16_t freq, uint8_t volume,
                             uint8_t duty, uint8_t sweep);
int  audio_p4_engine_note_off(uint8_t channel);

#ifdef __cplusplus
}
#endif
