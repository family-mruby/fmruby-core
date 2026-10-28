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

/* The desktop's sound server (sys_linux_pw.c, L14): PipeWire's wpctl,
 * else PulseAudio's pactl, on the default output, on the PC's machines
 * (system.settings). open runs the tool once to see that one answers (at
 * start, before the kernel: at most a few seconds when the server hangs)
 * and starts the thread that runs it from then on; false (and the reason)
 * when none answers. Then get answers from the last value known, and set
 * and mute take effect in it at once and in the server shortly after. */
bool sys_linux_pw_open(char *why, size_t cap);
bool sys_linux_pw_active(void);
const char *sys_linux_pw_name(void);
bool sys_linux_pw_get(int *step, bool *muted, int max_step);
bool sys_linux_pw_set(int step, int max_step);
bool sys_linux_pw_mute(bool on);
/* The pure parts, for the host tests. */
bool sys_linux_pw_parse_wpctl(const char *out, double *volume, bool *muted);
bool sys_linux_pw_parse_pactl_volume(const char *out, int *percent);
bool sys_linux_pw_parse_pactl_mute(const char *out, bool *muted);
int sys_linux_pw_step(double volume, int max_step);

/* The PC's settings and notices (L14), from main_native.c after install:
 * start the Wi-Fi and Bluetooth threads (they connect when first asked),
 * serve the notifications, and post the session's "restarted" notice when
 * FMRB_SESSION_RESTARTED says how the last run ended. */
void sys_linux_settings_start(void);

#ifdef __cplusplus
}
#endif
