/*
 * native_audio.h - the APU's samples, handed from the audio task to the
 * output.
 *
 * The APU task (FreeRTOS) writes mono int16 at NATIVE_AUDIO_RATE through the
 * audio_backend_t table; the output (SDL's audio callback, a thread of SDL's
 * own) reads them. One writer, one reader, no lock: two counters, each moved
 * by one side only.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the APU renders: 262 samples per 60 Hz frame. */
#define NATIVE_AUDIO_RATE 15720

/* Take up to n samples into out, padding with silence when the ring runs
 * dry. Called from the output's thread. */
void native_audio_read(int16_t *out, int n);

#ifdef __cplusplus
}
#endif
