// Local audio backend for the Modern (ESP32-P4 / M5Stack Tab5) target.
//
// The retro configuration synthesizes audio on the WROVER child chip;
// on Modern the same APU emulator (components/apu_emu) runs in-process
// and outputs through the Tab5 ES8388 codec (esp_codec_dev) over I2S.
//
// Init sequence (two stages, because the codec shares the I2C bus with
// PI4IO/GT911 which LovyanGFX drives at register level):
//   1. audio_backend()->init() - called from display_p4_task right after
//      LGFX init and BEFORE g_lcd_ready is set, i.e. before the touch
//      task starts polling GT911. Sets up I2S + ES8388 on the shared
//      I2C bus without concurrent bus users.
//   2. audio_p4_task_init() - called from boot; spawns the 60 Hz APU
//      task which waits until the hardware is ready.
//
// The hardware output path (init / ready / write / set_volume) is behind
// the audio_backend_t table in audio_backend.h.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fmrb_err.h"
#include "audio_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

// Spawn the audio task (safe to call before hw init completes).
fmrb_err_t audio_p4_task_init(void);

// Dispatch one packed audio command (see audio_commands.h).
// Called from the display task on FMRB_LINK_TYPE_AUDIO messages.
// Returns 0 on success, -1 on error.
int audio_p4_process_command(const uint8_t *data, size_t size);

// ---- Microphone (ES7210, doc/mic_spectrum) -------------------------------
//
// Public because Ruby reaches it directly: the samples are in this firmware,
// not on another board, so an app task calls these rather than asking the
// display task for them over the link.
//
// One reader at a time. The de-interleave buffer behind audio_p4_mic_read is
// a single static, so two tasks reading at once would tear each other's
// samples; one microphone with one listener is the shape this is built for.

// Is there a microphone on this hardware, ready to be turned on?
bool audio_p4_mic_available(void);

// Samples per second. Not selectable: the microphone shares the speaker's
// I2S clocks (see audio_p4_hw.c).
int audio_p4_mic_sample_rate(void);

// Power the codec up or down. FMRB_OK when it answered.
fmrb_err_t audio_p4_mic_enable(bool on);

// Read up to max_samples mono int16 samples (left channel of the pair).
// Returns the count, 0 on timeout, negative when the microphone is not on.
int audio_p4_mic_read(int16_t *dst, int max_samples, int timeout_ms);

#ifdef __cplusplus
}
#endif
