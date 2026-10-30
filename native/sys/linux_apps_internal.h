/*
 * linux_apps_internal.h - what the files of the launcher's Linux
 * applications share (L16): the entry, and the functions one of them calls
 * in another. linux_apps.c has the overview, the state and the interface
 * (linux_apps.h, the only way in for other parts); linux_apps_util.c,
 * linux_apps_exec.c and linux_apps_icons.c the rest. Not for other parts.
 * Include it first (it defines _GNU_SOURCE).
 */

#ifndef LINUX_APPS_INTERNAL_H
#define LINUX_APPS_INTERNAL_H

#define _GNU_SOURCE
#include <ctype.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <dlfcn.h>

#if NATIVE_HAVE_PNG
#include <png.h>
#endif

#include "linux_apps.h"
#include "linux_mime.h"

typedef struct {
    char *id;           /* desktop file id ("foot.desktop") */
    char *name;         /* Name, or Name[<lang>] */
    char *key;          /* StartupWMClass, else the program's file name */
    char *idkey;        /* the id without ".desktop" (Wayland app ids follow it) */
    char *exe;          /* the program, an absolute path */
    char **argv;        /* as Exec gives it (argv[0] as written) */
    char *dir;          /* Path=, or NULL */
    char *icon;         /* the icon BMP as a Family mruby path, or NULL */
    /* For opening a file in it (L11). */
    char *exec;         /* Exec as written */
    char *iconname;     /* Icon as written (%i) */
    char *file;         /* the .desktop file (%k) */
    char *prog;         /* Exec's program, an absolute path (exe is the terminal's for Terminal=true) */
    char *mime;         /* MimeType ("a/b;c/d;"), or NULL */
    bool terminal;      /* Terminal=true: run in the terminal */
    bool listed;        /* in the launcher's list (false: kept only to open files with) */
} entry_t;

/* The icon themes read for a list (linux_apps_icons.c, L9e). */
#define THEMES_MAX 8
#define THEME_DIRS_MAX 1024
#define ICON_BASES_MAX 18

typedef struct {
    char *path;     /* <base>/<theme>/<directory> */
    int dist;       /* from the launcher's size */
} theme_dir_t;

struct linux_icon_themes {
    char name[THEMES_MAX][64];
    int n;
    theme_dir_t *dir;   /* every theme's, theme by theme, each theme's nearest first */
    int ndir;
    int start[THEMES_MAX + 1];
};

/* linux_apps_util.c */
char *dup_str(const char *a);
void free_list(entry_t *e, int n);
char **copy_argv(char *const *argv);
uint64_t ms_now(void);
void sleep_ms(int ms);
void copy_utf8(char *out, size_t cap, const char *in);
bool find_program(const char *name, char *out, size_t cap);
bool pathf(char *out, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
const char *base_name(const char *path);
bool same_word(const char *a, const char *b);

/* linux_apps_exec.c */
void unescape_value(char *v);

/* linux_apps_icons.c */
bool has_suffix(const char *s, const char *suf);
char *icon_for(const char *icon, const linux_icon_themes_t *th, const char *icon_dir, const char *fm_dir,
               int *svg);

#endif /* LINUX_APPS_INTERNAL_H */
