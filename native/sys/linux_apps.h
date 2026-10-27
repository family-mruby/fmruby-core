/*
 * linux_apps.h - the Linux PC's applications, for the launcher's "Linux"
 * group (doc/linux_native, L9b).
 *
 * Only the compositor machine (linux_pc_wl) has them: the compositor starts
 * this module (linux_apps_start) and drives it from its main thread
 * (linux_apps_pump); until then, and on every other machine,
 * linux_apps_available() is false.
 *
 *   the list   the XDG desktop entries (.desktop) of $XDG_DATA_HOME and
 *              $XDG_DATA_DIRS (applications/), the first of each desktop
 *              file id winning. Left out: Type other than Application,
 *              NoDisplay=true, Hidden=true, OnlyShowIn without our desktop
 *              name, NotShowIn with it, a TryExec or an Exec program that is
 *              not there, Terminal=true with no terminal to run it in (foot,
 *              else x-terminal-emulator). The name is Name[<lang>] when there
 *              is one. Read on a thread of its own, never on the caller's.
 *   icons      the entry's Icon: an absolute PNG, or a PNG of that name in
 *              the hicolor theme (the size nearest 24 first) or pixmaps/,
 *              made a 24x24 RGB332 BMP (the launcher's sprites) in the icon
 *              directory, once per source file. None found: no icon (the
 *              launcher's own mark). A generic mark for the group itself is
 *              written there at start (linux.bmp).
 *   starting   by the entry's id: Exec split by the Desktop Entry rules
 *              (quoting, field codes; no files are passed), started without
 *              a shell on the compositor's main thread in a session of its
 *              own, with the program's environment (WAYLAND_DISPLAY and
 *              DISPLAY are the compositor's) and XDG_CURRENT_DESKTOP set to
 *              our desktop name. Never waited for: reaped as it ends.
 *   windows    a window's name for its stand-in: the entry of the started
 *              program whose session the window's process is in, else the
 *              entry whose StartupWMClass (or the program's file name, or the
 *              desktop file id) is the window's app id / WM_CLASS.
 *   the end    every session started here that still has a process gets
 *              SIGTERM, then SIGKILL (linux_apps_end_all).
 *
 * Plain POSIX and libpng (no FreeRTOS, no fmrb allocator): the host tests
 * (test/native/apps.c) run it as it is.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The launcher's icon size (LauncherMixin::ICON_SPRITE_W / _H). */
#define LINUX_APPS_ICON_PX 24

typedef struct {
    const char *icon_dir;       /* host directory for the BMPs (made if missing) */
    const char *icon_fm_dir;    /* the same directory as a Family mruby path */
    const char *desktop_name;   /* our name for OnlyShowIn / NotShowIn */
} linux_apps_config_t;

/* list() modes. */
#define LINUX_APPS_LIST 0       /* the header and the entries */
#define LINUX_APPS_RESCAN 1     /* read the entries again; the header only */
#define LINUX_APPS_HEADER 2     /* the header only */

/* ---- the compositor's side (its main thread) ---- */

/* Make the icon directory and the group's mark, and read the entries (on a
 * thread of its own). From here on available() is true. */
void linux_apps_start(const linux_apps_config_t *cfg);
/* Start what was asked for, reap what ended. Never waits. */
void linux_apps_pump(void);
/* The end: SIGTERM to every process of every session started here, SIGKILL
 * after grace_ms to what is left, the children reaped. Returns how many
 * processes were still there to be ended. Waits (the program is ending). */
int linux_apps_end_all(int grace_ms);
/* A window's name (see above); false when nothing matches. app_id and alt
 * may be NULL; pid <= 0 is unknown. */
bool linux_apps_window_name(const char *app_id, const char *alt, pid_t pid, char *out, size_t cap);
/* How many started programs have a process still (for the logs). */
int linux_apps_running(void);

/* ---- Family mruby's side (any thread; none of these waits) ---- */

bool linux_apps_available(void);
/* The list as text into buf (cap bytes, not NUL-terminated):
 *   "ready\t<group mark>\n" or "loading\t<group mark>\n", then for
 *   LINUX_APPS_LIST when ready one line per entry, sorted by name:
 *   "<name>\t<desktop file id>\t<icon BMP, Family mruby path, or empty>\n"
 * Returns the length (more than cap: nothing written, call again with
 * room), -1 when not available. A lang other than the last read's reads
 * the entries again. */
int32_t linux_apps_list(int mode, const char *lang, char *buf, size_t cap);
/* Ask for the entry with this id to be started (on the next pump). 0, or
 * -1: no such entry / too many waiting. */
int linux_apps_launch(const char *id);

/* ---- for the tests ---- */

/* Exec split by the Desktop Entry rules into a NULL-terminated, malloc'd
 * argv (linux_apps_free_argv). name, icon and file give %c, %i and %k. */
char **linux_apps_split_exec(const char *exec, const char *name, const char *icon, const char *file);
void linux_apps_free_argv(char **argv);
/* Wait (tests only) until no read of the entries is under way. */
bool linux_apps_wait_scan(int timeout_ms);
/* The pid of the last program started (tests). */
pid_t linux_apps_last_pid(void);

#ifdef __cplusplus
}
#endif
