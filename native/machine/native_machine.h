/*
 * native_machine.h - the machine definition of the native program
 * (doc/linux_native/plan.md, "機種の定義").
 *
 * One TOML file says which output, input and sound backends a machine uses,
 * the shape of its screen and its desktop. The program is started with
 * --machine <name or path>; a name is looked up in the distribution's
 * machines directory (config/machines/ is the source, rake native:etc stages
 * it), a path is read as it is. --set section.key=value changes one field
 * after the file, and the L1 options (--scale) still win over both.
 *
 *   [screen]   form           "426x240" (Modern) | "320x170" (the Cardputer
 *                             Zero's LCD) | "output" (wlroots only, L9d: the
 *                             output over display.scale, 1280x800 at scale 2
 *                             a 640x400 screen; a portrait output or one
 *                             under 426x240 at that scale gets 426x240, one
 *                             over 1920x1080 is kept within it).
 *                             The base screen: the kernel's display size
 *                             whatever the user's system_conf says
 *   [display]  backend        sdl | fbdev | drm | wlroots (the program is the
 *                             Wayland compositor, L8; input.backend is then
 *                             wlroots too)
 *              device         fbdev: "auto", "auto:<name in /proc/fb>" or a
 *                             path; drm: "auto" or a /dev/dri/card path
 *              mode           drm, wlroots: "WxH" or "WxH@Hz", "auto" = the
 *                             connector's preferred mode (wlroots: a custom
 *                             mode when the output does not list it -- the
 *                             headless and nested outputs take any size)
 *              rotate         0 / 90 / 180 / 270, clockwise
 *              scale          "auto" (largest whole scale that fits; the
 *                             window's magnification 3 on sdl) or a number;
 *                             with screen.form "output" the divisor of the
 *                             output (auto = 2)
 *              fullscreen     sdl: true opens the window full screen on its
 *                             output (the frame at the largest whole scale,
 *                             black around it); false a window (L7)
 *   [input]    backend        sdl | evdev | wlroots (the inject socket is
 *                             always on)
 *              devices        evdev: ["auto"] or paths (/dev/input/by-path/..)
 *              keymap         "" or "cardputer_zero": the extra table for the
 *                             devices whose name contains keymap_device
 *              keymap_device  substring of the evdev device name
 *              ime            the PC's IME for the Family mruby apps (L12b):
 *                             "none" (Family mruby's own kana input), "sdl"
 *                             (SDL's text input: ibus, fcitx; display sdl)
 *                             or "wayland" (the compositor serves the IME
 *                             protocols; display wlroots). Without an IME
 *                             present the kana input stays
 *   [audio]    backend        sdl | alsa | none
 *              device         alsa: the PCM name ("default")
 *              latency_ms     alsa: the device buffer (100)
 *              mixer          alsa: the mixer element the desktop's volume
 *                             moves: "auto" (Master, PCM, Speaker, ...),
 *                             "none" (the internal volume) or a name (L6)
 *   [desktop]  variant        mouse (the Modern desktop) | keys (the
 *                             keyboard-only desktop, system_desktop_keys)
 *              window_ops     true: the PC's window operations (L13,
 *                             native_winops.h): a double-click on a title
 *                             maximizes, Super+arrows lay the window in
 *                             front out; false (the Cardputer, the keys
 *                             desktop) none
 *   [system]   services       none | linux: the OS services the desktop
 *                             shows and uses (L6, native/sys/); none keeps
 *                             the simulation's answers
 *              exit_key       none | esc_long: with the keys desktop, Esc
 *                             held on the desktop ends the program
 *              power          exit | logind, or both as a list
 *                             (["logind", "exit"]): the desktop's power
 *                             entries, ending the program (the Cardputer:
 *                             back to APPLaunch; a PC session: back to the
 *                             login screen) and/or power off / restart /
 *                             suspend through logind
 *              power_log_only true: log the logind request instead of
 *                             sending it (every test; never power off a
 *                             development machine)
 *   [files]    host           /mnt/host, Linux's files read-write: "auto"
 *                             (the user's home, $HOME), a directory, or
 *                             "none" to leave it out (L7)
 *              root           /mnt/root, Linux's files read-only: "auto"
 *                             ("/"), a directory, or "none"
 *   [compositor] autostart    wlroots only (L8): one Linux program started
 *                             once the compositor is up, through /bin/sh -c,
 *                             with WAYLAND_DISPLAY set to the compositor's
 *                             own socket ("foot"); "" starts none. It is
 *                             ended with the program
 *              ime            wlroots only (L12b): the IME program started
 *                             once the compositor is up ("fcitx5"; "" none),
 *                             through /bin/sh -c, without DISPLAY. Not
 *                             installed: one line, and the kana input stays
 *              locker         wlroots only (L13): the screen lock program
 *                             (ext-session-lock: "swaylock"; "" none),
 *                             started through /bin/sh -c by the power menu's
 *                             Lock and logind's Lock / PrepareForSleep. Not
 *                             installed: no Lock entry
 *
 * Pure code over a struct (tomlc99 and libc only), so the host tests load
 * the shipped definitions (test/native/). Which of the named backends this
 * build has is decided in one other place, native_backends.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_MACHINE_MAX_DEVICES 8
#define NATIVE_MACHINE_STR 256

typedef struct {
    char name[64];                       /* file name without .toml, or "built-in" */
    char source[NATIVE_MACHINE_STR];     /* the file read, "" for the built-in */
    char description[NATIVE_MACHINE_STR];

    struct {
        char form[16];                   /* "426x240", or "output" */
        int w, h;                        /* 0x0 for "output" (known once the output is) */
    } screen;

    struct {
        char backend[16];
        char device[NATIVE_MACHINE_STR];
        char mode[32];                   /* "auto" or "1280x720[@60]" */
        int rotate;
        int scale;                       /* 0 = auto */
        bool fullscreen;                 /* sdl only */
    } display;

    struct {
        char backend[16];
        char devices[NATIVE_MACHINE_MAX_DEVICES][NATIVE_MACHINE_STR];
        int n_devices;
        char keymap[32];
        char keymap_device[64];
        char ime[16];                    /* "none", "sdl" or "wayland" */
    } input;

    struct {
        char backend[16];
        char device[128];
        int latency_ms;
        char mixer[64];
    } audio;

    struct {
        char variant[16];
        bool window_ops;                 /* the PC's window operations (L13) */
    } desktop;

    struct {
        char services[16];
        char exit_key[16];
        char power[32];                  /* "exit", "logind" or "logind,exit" */
        bool power_log_only;
    } system;

    struct {
        char host[NATIVE_MACHINE_STR];   /* "auto", "none" or a directory */
        char root[NATIVE_MACHINE_STR];
    } files;

    struct {
        char autostart[NATIVE_MACHINE_STR];
        char ime[NATIVE_MACHINE_STR];    /* the IME program, "" none */
        char locker[NATIVE_MACHINE_STR]; /* the screen lock program, "" none (L13) */
    } compositor;
} native_machine_t;

/* The built-in definition: the same as config/machines/sdl.toml (the L1
 * program). */
void native_machine_defaults(native_machine_t *m);

/* One field by its dotted name ("display.backend"), from text: numbers in
 * decimal, a list as comma-separated items. Returns 0, or -1 with the
 * reason in err. */
int native_machine_set(native_machine_t *m, const char *key, const char *value,
                       char *err, size_t err_len);

/* Read a definition file over m (start from native_machine_defaults). Every
 * key must be known. Returns 0, or -1 with the reason in err. */
int native_machine_load_file(native_machine_t *m, const char *path,
                             char *err, size_t err_len);

/* The file a --machine argument names: a value with a '/' or ending in
 * ".toml" is a path, anything else <machines_dir>/<name>.toml. */
void native_machine_resolve(const char *arg, const char *machines_dir,
                            char *out, size_t out_len);

/* Values every build refuses, whatever backends it has: the screen forms
 * and desktops that do not exist yet. Returns 0, or -1 with the reason. */
int native_machine_check(const native_machine_t *m, char *err, size_t err_len);

/* Whether the screen is the output over display.scale (screen.form
 * "output"). */
bool native_machine_screen_from_output(const native_machine_t *m);

/* The screen for an output of out_w x out_h (screen.form "output"): the
 * output over display.scale (auto 2), or 426x240 when the output is portrait
 * or that would be smaller than 426x240. why (may be NULL) says which, for
 * the log. */
void native_machine_output_screen(const native_machine_t *m, int out_w, int out_h, int *w, int *h,
                                  char *why, size_t why_len);

/* Whether system.power lists this group ("exit" or "logind"). */
bool native_machine_power_has(const native_machine_t *m, const char *group);

/* One line for the log. */
void native_machine_describe(const native_machine_t *m, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
