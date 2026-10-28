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
 * power actions; and (L10) Linux programs under a pseudo terminal, for the
 * Shell's "!" lines.
 *
 * And (L9b) the Linux PC's applications for the launcher, where the program
 * is the Wayland compositor.
 *
 * Every entry may be NULL; fmrb_platform_sys_*() below then answer "no
 * value" (false, 0). None of them may block for long: the hosted builds run
 * a cooperative scheduler, and a call that waits on the OS goes through the
 * blocking-call gate (hosted/gate) inside the implementation.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
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
    FMRB_SYS_POWER_LOCK     = 1u << 5,  /* lock the screen (the PC's lock program, L13) */
} fmrb_sys_power_t;

/** A Linux program to start under a pseudo terminal (L10). */
typedef struct {
    const char *command;  /* run with /bin/sh -c; NULL: the interactive shell */
    const char *cwd;      /* the Family mruby path to work in (resolved by the
                           * file namespace; a place outside it is refused) */
    uint16_t cols;        /* the terminal's size */
    uint16_t rows;
} fmrb_sys_proc_spec_t;

/** Signals an app may send to its program. */
typedef enum {
    FMRB_SYS_SIG_INT = 1,   /* Ctrl+C */
    FMRB_SYS_SIG_TERM,
    FMRB_SYS_SIG_KILL,
    FMRB_SYS_SIG_HUP,
} fmrb_sys_signal_t;

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

    /* Linux programs (L10). None of these waits: the terminal is read and
     * written without blocking. NULL proc_start: this machine runs none.
     * The program belongs to the calling task; it is ended when the task is
     * deleted. why (may be NULL) says why a start failed. */
    fmrb_err_t (*proc_start)(const fmrb_sys_proc_spec_t *spec, int32_t *id,
                             char *why, size_t why_cap);
    /* > 0 bytes read; 0 nothing just now; -1 no more output. */
    int32_t (*proc_read)(int32_t id, void *buf, size_t cap);
    /* Bytes taken (0: the input is full just now); -1 it takes no more. */
    int32_t (*proc_write)(int32_t id, const void *buf, size_t len);
    fmrb_err_t (*proc_signal)(int32_t id, fmrb_sys_signal_t sig);
    /* true once it ended; *code is the exit status (128 + signal). */
    bool (*proc_exited)(int32_t id, int *code);
    fmrb_err_t (*proc_resize)(int32_t id, uint16_t cols, uint16_t rows);
    /* Forget it; one still running is ended without the caller waiting. */
    void (*proc_close)(int32_t id);

    /* The Linux PC's applications (L9b), where the program is the
     * compositor. apps_list writes the launcher's list as text into buf
     * (native/sys/linux_apps.h: a header line, then "name\tid\ticon" per
     * entry) and returns its length -- more than cap: nothing written, ask
     * again with room -- or -1 when this machine has none. mode: 0 the list,
     * 1 read the entries again, 2 the header only. apps_launch asks for the
     * entry with that id to be started; neither waits. */
    int32_t (*apps_list)(int mode, const char *lang, char *buf, size_t cap);
    fmrb_err_t (*apps_launch)(const char *id);

    /* Opening a file in a Linux application (L11). path is a Family mruby
     * path (/mnt/host/..., /home/...), made the file's place on Linux
     * here; one that has none is refused. apps_handlers writes the
     * applications that open it, the default first, as text into buf
     * (native/sys/linux_apps.h: "type\t<type>" then "name\tid" per entry)
     * and returns its length (more than cap: nothing written, ask again
     * with room), or -1 when this machine has none or the file has no place
     * on Linux. apps_open asks for it to be opened in the entry with that
     * id ("": the default for its type); the entry's name goes into name,
     * the reason it did not into why. Neither waits for the application. */
    int32_t (*apps_handlers)(const char *path, char *buf, size_t cap);
    fmrb_err_t (*apps_open)(const char *id, const char *path, char *name, size_t name_cap,
                            char *why, size_t why_cap);
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
/* Linux programs: proc_available() false on every machine without them (the
 * devices, the simulation, the browser), where the rest answer
 * FMRB_ERR_NOT_SUPPORTED / -1 / true (ended). */
bool fmrb_platform_sys_proc_available(void);
fmrb_err_t fmrb_platform_sys_proc_start(const fmrb_sys_proc_spec_t *spec, int32_t *id,
                                        char *why, size_t why_cap);
int32_t fmrb_platform_sys_proc_read(int32_t id, void *buf, size_t cap);
int32_t fmrb_platform_sys_proc_write(int32_t id, const void *buf, size_t len);
fmrb_err_t fmrb_platform_sys_proc_signal(int32_t id, fmrb_sys_signal_t sig);
bool fmrb_platform_sys_proc_exited(int32_t id, int *code);
fmrb_err_t fmrb_platform_sys_proc_resize(int32_t id, uint16_t cols, uint16_t rows);
void fmrb_platform_sys_proc_close(int32_t id);
/* The Linux PC's applications: -1 / FMRB_ERR_NOT_SUPPORTED on every
 * machine without them. */
int32_t fmrb_platform_sys_apps_list(int mode, const char *lang, char *buf, size_t cap);
fmrb_err_t fmrb_platform_sys_apps_launch(const char *id);
/* Opening a file in a Linux application: -1 / FMRB_ERR_NOT_SUPPORTED on
 * every machine without them. */
int32_t fmrb_platform_sys_apps_handlers(const char *path, char *buf, size_t cap);
fmrb_err_t fmrb_platform_sys_apps_open(const char *id, const char *path, char *name, size_t name_cap,
                                       char *why, size_t why_cap);

#ifdef __cplusplus
}
#endif
