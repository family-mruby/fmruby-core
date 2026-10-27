/*
 * sys_linux_read.h - what the native program reads about the machine from
 * sysfs and procfs (doc/linux_native, L6): the battery and the network
 * link; and the keyboard layout from Linux's settings (L9d). Read only,
 * files only, no device opened and nothing written.
 *
 * The roots are arguments ("/sys", "/proc" in the program) so the host
 * tests read a made-up tree (test/native/sys/) the same way.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int percent;        /* 0-100 */
    bool charging;
    char name[32];      /* the power_supply entry, "BAT0" */
} sys_linux_battery_t;

/* The first power_supply whose type is Battery (and present, when it says)
 * and that tells a charge: capacity, else charge_now/charge_full, else
 * energy_now/energy_full. Returns true when there is one. */
bool sys_linux_battery_read(const char *sysfs, sys_linux_battery_t *out);

typedef struct {
    char iface[32];     /* the interface reported ("" when there is none) */
    bool connected;     /* it carries the default route and is up */
    bool wireless;      /* it has a wireless directory in sysfs */
    int signal;         /* 0-100 from /proc/net/wireless, -1 unknown */
} sys_linux_link_t;

/* The link the status bar shows: the interface of the default route
 * (/proc/net/route) when it is up; else, not connected, the first
 * wireless interface, else the first interface that is not loopback.
 * Returns true when there is any interface but loopback. */
bool sys_linux_link_read(const char *sysfs, const char *procfs, sys_linux_link_t *out);

/* The link quality of iface in /proc/net/wireless as 0-100 (the link column
 * over 70, the scale cfg80211 reports), -1 when the line is not there. */
int sys_linux_wireless_signal(const char *procfs, const char *iface);

/* ALSA mixer values <-> the internal volume steps 0..max_step, linear over
 * the element's raw range. */
int sys_linux_mixer_to_step(long value, long min, long max, int max_step);
long sys_linux_step_to_mixer(int step, long min, long max, int max_step);

/* The keyboard layout Linux is set to (L9d), for keyboard_layout = "auto":
 * the user's Plasma setting first (kxkbrc's [Layout]: LayoutList's first
 * when Use=true), else the system's (/etc/default/keyboard's XKBLAYOUT,
 * which localectl and dpkg-reconfigure keyboard-configuration write; its
 * first when it lists several). "jp" when that is jp, "us" otherwise or
 * when neither says. how (may be NULL) says where it came from. Either path
 * may be NULL (not read). */
const char *sys_linux_keyboard_layout(const char *kxkbrc, const char *keyboard, char *how, size_t how_len);

/* The logind method a power action asks for (PowerOff, Reboot, Suspend),
 * NULL for one logind does not do. action is an FMRB_SYS_POWER_* bit. */
const char *sys_linux_logind_method(unsigned action);

#ifdef __cplusplus
}
#endif
