// Local audio backend for the Modern (ESP32-P4 / M5Stack Tab5) target.
//
// The retro configuration synthesizes audio on the WROVER child chip;
// on Modern the same APU emulator (components/apu_emu) runs in-process
// and outputs through the Tab5 ES8388 codec (esp_codec_dev) over I2S.
//
// Init sequence (two stages, because the codec shares the I2C bus with
// PI4IO/GT911 which LovyanGFX drives at register level):
//   1. audio_p4_hw_init()   - called from display_p4_task right after
//      LGFX init and BEFORE g_lcd_ready is set, i.e. before the touch
//      task starts polling GT911. Sets up I2S + ES8388 on the shared
//      I2C bus without concurrent bus users.
//   2. audio_p4_task_init() - called from boot; spawns the 60 Hz APU
//      task which waits until the hardware is ready.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Codec + I2S bring-up. Display task context only (see above).
fmrb_err_t audio_p4_hw_init(void);

// Spawn the audio task (safe to call before hw init completes).
fmrb_err_t audio_p4_task_init(void);

// Dispatch one packed audio command (see audio_commands.h).
// Called from the display task on FMRB_LINK_TYPE_AUDIO messages.
// Returns 0 on success, -1 on error.
int audio_p4_process_command(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif
