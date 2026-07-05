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
// Poll the headphone jack and mute/unmute the speaker amp (call ~2 Hz)
void audio_p4_hw_poll_headphone(void);

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
