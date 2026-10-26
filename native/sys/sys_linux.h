/*
 * sys_linux.h - the native program's Linux OS services (doc/linux_native,
 * L6): the table (platform_sys_linux.c) and the ALSA mixer behind its volume
 * (sys_linux_mixer.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Install the Linux table (fmrb_platform_sys_install). main_native.c, for
 * system.services = "linux", after the audio output is open. */
void sys_linux_install(void);

/* Open the mixer element the machine names (audio.mixer: "auto", "none" or
 * an element name) on the card of audio.device. Only for the ALSA output;
 * false (and the reason in why) when there is none to open -- the internal
 * volume then stays in charge. */
bool sys_linux_mixer_open(const char *device, const char *element, char *why, size_t cap);
void sys_linux_mixer_close(void);
/* "Master on default", or "none". */
const char *sys_linux_mixer_name(void);

/* The volume as internal steps 0..max_step and the mute; false without a
 * mixer. */
bool sys_linux_mixer_get(int *step, bool *muted, int max_step);
bool sys_linux_mixer_set(int step, int max_step);
bool sys_linux_mixer_mute(bool on);

#ifdef __cplusplus
}
#endif
