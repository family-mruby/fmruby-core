/*
 * host_sdl.h - the native program's main-thread loop on SDL2.
 *
 * Owns everything SDL: the window (created once the display task says how
 * big its frame is), the presentation of native_frame.c through the shared
 * scaler (native/present/), the window's input into the sdl input source,
 * and the audio device draining native_audio.h. Runs on the process's main
 * thread, outside FreeRTOS, until the window is closed, a signal arrives or
 * the control socket says quit. Headless runs use SDL_VIDEODRIVER=dummy and
 * SDL_AUDIODRIVER=disk (or dummy); nothing here changes for them.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Bring SDL up. False when the video subsystem cannot start at all. */
int host_sdl_init(void);

/* The loop. Returns the process exit code. */
int host_sdl_run(void);

#ifdef __cplusplus
}
#endif
