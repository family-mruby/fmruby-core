/*
 * audio_backend_native.c - the native program's audio output behind the
 * audio_backend_t table (doc/linux_native/).
 *
 * The browser build's shape (wasm/backend/audio_backend_wasm.c): write()
 * pushes the APU's mono int16 into a ring, and the consumer -- here SDL's
 * audio callback or the ALSA thread (native/audio/), there an AudioWorklet --
 * drains it at its own pace. The APU task paces itself on the wall clock (audio_p4_task.c), so the
 * writer never blocks.
 *
 * The two clocks drift apart slowly, so the reader keeps the queue between a
 * low and a high mark: above the high mark it skips ahead to the low one (a
 * late consumer, or an output that started after the machine), below zero it
 * pads with silence.
 */

#include <stdatomic.h>
#include <string.h>

#include "fmrb_err.h"
#include "fmrb_log.h"
#include "audio_backend.h"
#include "native_audio.h"

static const char *TAG = "audio_native";

/* About 1 s, power of two for cheap wraparound. */
#define RING_SAMPLES 16384
/* Queue the reader aims for: ~65 ms, and no more than ~250 ms. */
#define RING_LOW     1024
#define RING_HIGH    4096

static int16_t s_ring[RING_SAMPLES];
static atomic_uint s_wr;    /* moved by the writer (the APU task) */
static atomic_uint s_rd;    /* moved by the reader (the output) */
static atomic_int s_ready;

void native_audio_read(int16_t *out, int n)
{
    unsigned wr = atomic_load_explicit(&s_wr, memory_order_acquire);
    unsigned rd = atomic_load_explicit(&s_rd, memory_order_relaxed);
    if (wr - rd > RING_HIGH) {
        rd = wr - RING_LOW;
    }
    int i = 0;
    for (; i < n && rd != wr; i++) {
        out[i] = s_ring[rd++ & (RING_SAMPLES - 1)];
    }
    for (; i < n; i++) {
        out[i] = 0;
    }
    atomic_store_explicit(&s_rd, rd, memory_order_release);
}

static fmrb_err_t audio_native_init(void)
{
    atomic_store(&s_ready, 1);
    FMRB_LOGI(TAG, "native audio backend up (%d Hz mono ring, %d samples)",
              NATIVE_AUDIO_RATE, RING_SAMPLES);
    return FMRB_OK;
}

static bool audio_native_ready(void)
{
    return atomic_load(&s_ready) != 0;
}

static void audio_native_write(const int16_t *samples, int len, int channels)
{
    if (!samples || len <= 0 || channels != 1) return;
    unsigned wr = atomic_load_explicit(&s_wr, memory_order_relaxed);
    for (int i = 0; i < len; i++) {
        s_ring[wr++ & (RING_SAMPLES - 1)] = samples[i];
    }
    atomic_store_explicit(&s_wr, wr, memory_order_release);
}

static const audio_backend_t s_backend_native = {
    .name  = "native",
    .init  = audio_native_init,
    .ready = audio_native_ready,
    .write = audio_native_write,
};

const audio_backend_t *audio_backend(void)
{
    return &s_backend_native;
}
