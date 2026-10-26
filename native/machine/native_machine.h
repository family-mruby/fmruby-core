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
 *                             Zero's LCD). The base screen: the kernel's
 *                             display size whatever the user's system_conf says
 *   [display]  backend        sdl | fbdev | drm
 *              device         fbdev: "auto", "auto:<name in /proc/fb>" or a
 *                             path; drm: "auto" or a /dev/dri/card path
 *              mode           drm: "WxH" or "WxH@Hz", "auto" = the
 *                             connector's preferred mode
 *              rotate         0 / 90 / 180 / 270, clockwise
 *              scale          "auto" (largest whole scale that fits; the
 *                             window's magnification 3 on sdl) or a number
 *   [input]    backend        sdl | evdev (the inject socket is always on)
 *              devices        evdev: ["auto"] or paths (/dev/input/by-path/..)
 *              keymap         "" or "cardputer_zero": the extra table for the
 *                             devices whose name contains keymap_device
 *              keymap_device  substring of the evdev device name
 *   [audio]    backend        sdl | alsa | none
 *              device         alsa: the PCM name ("default")
 *              latency_ms     alsa: the device buffer (100)
 *   [desktop]  variant        mouse (the Modern desktop) | keys (the
 *                             keyboard-only desktop, system_desktop_keys)
 *   [system]   services       none | linux (L6; accepted, not implemented)
 *              exit_key       none | esc_long: with the keys desktop, Esc
 *                             held on the desktop ends the program
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
        char form[16];                   /* "426x240" */
        int w, h;
    } screen;

    struct {
        char backend[16];
        char device[NATIVE_MACHINE_STR];
        char mode[32];                   /* "auto" or "1280x720[@60]" */
        int rotate;
        int scale;                       /* 0 = auto */
    } display;

    struct {
        char backend[16];
        char devices[NATIVE_MACHINE_MAX_DEVICES][NATIVE_MACHINE_STR];
        int n_devices;
        char keymap[32];
        char keymap_device[64];
    } input;

    struct {
        char backend[16];
        char device[128];
        int latency_ms;
    } audio;

    struct {
        char variant[16];
    } desktop;

    struct {
        char services[16];
        char exit_key[16];
    } system;
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

/* One line for the log. */
void native_machine_describe(const native_machine_t *m, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
