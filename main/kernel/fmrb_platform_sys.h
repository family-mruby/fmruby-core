/*
 * fmrb_platform_sys.h - what the machine's operating system can tell and do
 * for the desktop (doc/linux_native, L6): the network status, the battery,
 * the volume, and the power actions.
 *
 * One function table, like display_backend_t, audio_backend_t and the input
 * sources. The default table (fmrb_platform_sys.c) is what every target did
 * before: the device's own WiFi, the simulation's and the browser's view of
 * the host network, the internal volume, and the device reset. The native
 * Linux program installs its own (native/sys/) when the machine definition
 * says system.services = "linux": it reads the battery and the network from
 * sysfs and procfs, the volume from the ALSA mixer, and asks logind for the
 * power actions.
 *
 * Every entry may be NULL; fmrb_platform_sys_*() below then answer "no
 * value" (false, 0). None of them may block for long: the hosted builds run
 * a cooperative scheduler, and a call that waits on the OS goes through the
 * blocking-call gate (hosted/gate) inside the implementation.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The network as the status bar and the network dialog show it. */
typedef struct {
    bool connected;
    char ip[16];        /* IPv4, dotted ("" when none) */
    char ssid[33];      /* the access point ("" wired or unknown) */
    char hostname[32];  /* the mDNS / host name ("" when none) */
    int8_t signal;      /* 0-100, -1 when unknown (wired, or not told) */
} fmrb_sys_net_t;

/** The battery: a charge in percent and whether it is charging. */
typedef struct {
    uint8_t percent;    /* 0-100 */
    bool charging;
} fmrb_sys_battery_t;

/** Power actions, as bits of power_actions() and the argument of power(). */
typedef enum {
    FMRB_SYS_POWER_RESET    = 1u << 0,  /* the device's reset (FmrbApp.reboot) */
    FMRB_SYS_POWER_EXIT     = 1u << 1,  /* end the program (back to the launcher) */
    FMRB_SYS_POWER_OFF      = 1u << 2,  /* power the computer off */
    FMRB_SYS_POWER_RESTART  = 1u << 3,  /* restart the computer */
    FMRB_SYS_POWER_SUSPEND  = 1u << 4,  /* suspend the computer */
} fmrb_sys_power_t;

typedef struct {
    const char *name;

    /* The network. false: this machine has no network status to give (the
     * desktop greys the icon out). */
    bool (*net_info)(fmrb_sys_net_t *out);
    /* Only the connected flag, allocation free (the 1 Hz icon). */
    bool (*net_connected)(void);

    /* The battery. false: no battery, or nothing to tell. */
    bool (*battery)(fmrb_sys_battery_t *out);

    /* The operating system's own volume (the ALSA mixer). Each returns
     * false when there is none, and the caller falls back to the internal
     * volume (doc/reference/audio_output.md). step is 0..max_step. */
    bool (*volume_get)(int *step, bool *muted, int max_step);
    bool (*volume_set)(int step, int max_step);
    bool (*mute_set)(bool on);

    /* The actions power() takes, FMRB_SYS_POWER_* bits. */
    uint32_t (*power_actions)(void);
    /* Do one. Returns only when it did not happen (or is under way and the
     * program goes on until the OS stops it). */
    fmrb_err_t (*power)(fmrb_sys_power_t action);
} fmrb_platform_sys_t;

/** The table in use: the default one unless a platform installed its own. */
const fmrb_platform_sys_t *fmrb_platform_sys(void);

/** A platform's own table, before the kernel starts (native: main_native.c). */
void fmrb_platform_sys_install(const fmrb_platform_sys_t *sys);

/* Callers' shorthands: a NULL entry answers false / 0. */
bool fmrb_platform_sys_net_info(fmrb_sys_net_t *out);
bool fmrb_platform_sys_net_connected(void);
bool fmrb_platform_sys_battery(fmrb_sys_battery_t *out);
bool fmrb_platform_sys_volume_get(int *step, bool *muted, int max_step);
bool fmrb_platform_sys_volume_set(int step, int max_step);
bool fmrb_platform_sys_mute_set(bool on);
uint32_t fmrb_platform_sys_power_actions(void);
fmrb_err_t fmrb_platform_sys_power(fmrb_sys_power_t action);

#ifdef __cplusplus
}
#endif
