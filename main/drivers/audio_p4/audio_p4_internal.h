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

// The microphone's public API (available / sample_rate / enable / read) lives
// in audio_p4.h: Ruby reaches it directly. What stays here is the bring-up
// instrumentation, which nothing outside this driver should call.
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
// Stops every player (NSF and both FMSQ instances) and silences the APU.
void audio_p4_engine_stop_all(void);
// instance picks the APU instance the FMSQ plays on: 0 = MAIN (shared with
// NSF), 1 = SUB (shared with the note_on/off effects).
int  audio_p4_engine_fmsq_play_slot(uint32_t music_id, uint8_t instance);
int  audio_p4_engine_note_on(uint8_t channel, uint16_t freq, uint8_t volume,
                             uint8_t duty, uint8_t sweep);
int  audio_p4_engine_note_off(uint8_t channel);

#ifdef __cplusplus
}
#endif
