// Internal interfaces between the audio_p4 hw / handler / task units.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "audio_p4.h"
#include "audio_commands.h"

// This driver opens sound files with plain fopen() rather than through the
// fmrb file HAL (the players want a FILE*), so it has to spell the storage
// root itself. On the device the VFS mounts LittleFS at /flash; the wasm
// build reaches the same tree through the POSIX HAL's cwd-relative "flash"
// directory. Same split, same reason, as LOCAL_BASE_PATH in
// main/kernel/host/host_file_local.c -- and getting it wrong is silent:
// the file simply fails to open and the music does not play.
#ifdef FMRB_PLATFORM_WASM
#define AUDIO_P4_FLASH_BASE "flash"
#else
#define AUDIO_P4_FLASH_BASE "/flash"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- audio_p4_hw.c ---
// The output path (init / ready / write) is reached through
// audio_backend() (audio_backend.h, included via audio_p4.h).
//
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
// Play a WAV file (PCM 16-bit mono, 8-48 kHz, up to FMRB_WAV_MAX_BYTES) from
// a LittleFS path, mixed on top of whatever the APU is doing. Starting one
// while another plays replaces it. Returns 0 when it started.
int  audio_p4_engine_play_wav(const char *path);
void audio_p4_engine_stop_wav(void);

// Output settings (doc/audio_mute/): mute and volume, applied where the frame
// loop writes the mix out -- everything upstream keeps running. The task reads
// them from system_conf before its first frame (so the boot beep obeys them);
// afterwards FMRB_AUDIO_CMD_SET_OUTPUT from the core sets them.
//   silent:   write silence (muted, or volume 0)
//   gain_q16: software gain on the samples, 65536 = unity. Unity on the device,
//             where the codec's own volume does the work.
void audio_p4_out_set(bool silent, uint32_t gain_q16);
bool audio_p4_out_silent(void);
// A level in tenths of a dB as a software gain (65536 = unity; levels above
// 0 dB are held at unity, a gain above 1 would only clip).
uint32_t audio_p4_gain_q16(int16_t db_x10);

#if defined(FMRB_HW_MODERN)
// --- audio_p4_hw.c: the codec's own output level and mute ---
// db is the chip's DAC level (clamped to what it can do). Command path
// (display task) only, where codec I2C belongs.
void audio_p4_hw_set_out_db(float db);
void audio_p4_hw_set_out_mute(bool mute);
#endif

#ifdef __cplusplus
}
#endif
