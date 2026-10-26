/*
 * audio_sdl.c - the SDL audio device as a sink (native_backends.h), and the
 * sink that plays nothing.
 *
 * SDL's audio thread calls back for samples and takes them from the ring
 * (native_audio.h); SDL converts the APU's 15720 Hz to the device's rate.
 * Headless runs use SDL_AUDIODRIVER=disk (tools/fmrb_native.rb). As in L1, a
 * machine without a working SDL audio device runs silent with a warning
 * rather than not at all: this is the development sink, used on PCs whose
 * sound setup is not the point.
 */

#include <SDL.h>
#include <stdio.h>

#include "native_audio.h"
#include "native_backends.h"

static SDL_AudioDeviceID s_audio = 0;

static void audio_callback(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    native_audio_read((int16_t *)stream, len / (int)sizeof(int16_t));
}

static bool sdl_audio_open(const native_machine_t *m, char *why, size_t why_len)
{
    (void)m;
    (void)why;
    (void)why_len;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "native: no audio (%s); running silent\n", SDL_GetError());
        return true;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = NATIVE_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_callback;
    /* No allowed changes: SDL converts the APU's rate to the device's. */
    s_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s_audio == 0) {
        fprintf(stderr, "native: cannot open audio (%s); running silent\n", SDL_GetError());
        return true;
    }
    SDL_PauseAudioDevice(s_audio, 0);
    fprintf(stderr, "native: audio on '%s' (%d Hz from %d Hz)\n",
            SDL_GetCurrentAudioDriver(), have.freq, NATIVE_AUDIO_RATE);
    return true;
}

static void sdl_audio_close(void)
{
    if (s_audio != 0) SDL_CloseAudioDevice(s_audio);
    s_audio = 0;
}

const native_audio_sink_t native_audio_sdl = {
    .name  = "sdl",
    .open  = sdl_audio_open,
    .close = sdl_audio_close,
};

/* ---- none: the ring fills and wraps; the APU never waits for it -------- */

static bool none_open(const native_machine_t *m, char *why, size_t why_len)
{
    (void)m;
    (void)why;
    (void)why_len;
    fprintf(stderr, "native: audio none (silent)\n");
    return true;
}

static void none_close(void)
{
}

const native_audio_sink_t native_audio_none = {
    .name  = "none",
    .open  = none_open,
    .close = none_close,
};
