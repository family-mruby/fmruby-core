/*
 * audio_backend_wasm.c - the wasm audio output (doc/wasm/ P4a-P4c).
 *
 * Fills the audio_backend_t table the APU task drives. The APU renders mono
 * int16 at 15720 Hz; write() pushes those samples into a lock-free ring in
 * wasm memory. In the browser (P4c) an AudioWorklet drains the ring and
 * resamples to the output rate; under node nothing reads it and the writer
 * just keeps overwriting the oldest samples -- the APU never blocks.
 */

#include <string.h>

#include "fmrb_err.h"
#include "fmrb_log.h"
#include "audio_backend.h"

#include <emscripten/emscripten.h>

static const char *TAG = "audio_wasm";

/* About 1 s at 15720 Hz, power of two for cheap wraparound. The plan asks for
 * a ring longer than the device's 130 ms; the worklet keeps its own read
 * cursor and decides how much latency to run with. */
#define AUDIO_WASM_RING_SAMPLES 16384

static int16_t s_ring[AUDIO_WASM_RING_SAMPLES];
static volatile uint32_t s_wr = 0;   /* total samples ever written */
static volatile uint8_t s_volume = 255;
static volatile int s_ready = 0;

/* ---- the JS-facing surface (P4c's AudioWorklet reads these) ------------- */

EMSCRIPTEN_KEEPALIVE const int16_t *fmrb_wasm_audio_ring(void) { return s_ring; }
EMSCRIPTEN_KEEPALIVE uint32_t fmrb_wasm_audio_ring_size(void) { return AUDIO_WASM_RING_SAMPLES; }
EMSCRIPTEN_KEEPALIVE uint32_t fmrb_wasm_audio_wr(void) { return s_wr; }
EMSCRIPTEN_KEEPALIVE uint32_t fmrb_wasm_audio_rate(void) { return 15720; }
EMSCRIPTEN_KEEPALIVE uint32_t fmrb_wasm_audio_volume(void) { return s_volume; }

/* ------------------------------------------------------------------------ */

static fmrb_err_t audio_wasm_init(void)
{
    s_ready = 1;
    FMRB_LOGI(TAG, "wasm audio backend up (15720 Hz ring, %d samples)",
              AUDIO_WASM_RING_SAMPLES);
    return FMRB_OK;
}

static bool audio_wasm_ready(void)
{
    return s_ready != 0;
}

static void audio_wasm_write(const int16_t *samples, int len, int channels)
{
    if (!samples || len <= 0 || channels != 1) return;
    uint32_t wr = s_wr;
    for (int i = 0; i < len; i++) {
        s_ring[wr++ & (AUDIO_WASM_RING_SAMPLES - 1)] = samples[i];
    }
    s_wr = wr;
}

static void audio_wasm_set_volume(uint8_t volume_0_255)
{
    s_volume = volume_0_255;
}

static const audio_backend_t s_backend_wasm = {
    .name       = "wasm",
    .init       = audio_wasm_init,
    .ready      = audio_wasm_ready,
    .write      = audio_wasm_write,
    .set_volume = audio_wasm_set_volume,
};

const audio_backend_t *audio_backend(void)
{
    return &s_backend_wasm;
}
