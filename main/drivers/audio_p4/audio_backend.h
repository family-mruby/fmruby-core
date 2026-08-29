// audio_backend.h - output side of the Modern (ESP32-P4) audio driver
//
// The APU emulator is already half-abstracted from the hardware: it renders
// into a writer function installed with apuif_set_output_writer(). This table
// gathers the other hardware touchpoints (bring-up, readiness, volume) with
// that writer, so a target without the ES8388 codec (wasm, doc/wasm/ P3) can
// supply all four in one place. The microphone stays outside: it is Tab5
// hardware with its own public API in audio_p4.h, not part of the output path.
//
// One implementation today, in audio_p4_hw.c. There is no build-time selection
// yet; the wasm target brings its own table when it arrives.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;

    // Codec + I2S bring-up. Display task context only: the codec shares the
    // I2C bus with PI4IO/GT911, so this runs right after LGFX init and before
    // the touch task starts polling (see audio_p4.h).
    fmrb_err_t (*init)(void);

    // Has init completed? The APU task spins on this before installing write.
    bool (*ready)(void);

    // Push one block of interleaved int16 samples to the output.
    // Installed into the APU emulator via apuif_set_output_writer().
    void (*write)(const int16_t *samples, int len, int channels);

    void (*set_volume)(uint8_t volume_0_255);
} audio_backend_t;

const audio_backend_t *audio_backend(void);

#ifdef __cplusplus
}
#endif
