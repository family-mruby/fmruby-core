/*
 * linux_mime.h - a file's type and the Linux PC's associations of types to
 * applications (doc/linux_native, L11), for opening a file in a Linux
 * application (linux_apps.c composes these with its desktop entries).
 *
 *   the type       shared-mime-info's globs2 in each data directory
 *                  ($XDG_DATA_HOME, $XDG_DATA_DIRS: mime/globs2): the
 *                  matching pattern of the highest weight, the longest of
 *                  those; a pattern marked "cs" against the name as it is,
 *                  any other against the name in lower case (xdgmime's way).
 *                  No globs2 anywhere: a short table of extensions. Nothing
 *                  matches: no type (the file's contents are not looked at).
 *   its kinds      the type, its canonical name (mime/aliases), what it is a
 *                  subclass of (mime/subclasses, breadth first) and, for
 *                  text/..., text/plain.
 *   the lists      mimeapps.list in the spec's order: $XDG_CONFIG_HOME,
 *                  $XDG_CONFIG_DIRS, $XDG_DATA_HOME/applications,
 *                  $XDG_DATA_DIRS/applications, in each the desktops' own
 *                  <desktop>-mimeapps.list (ours, then the machine's own
 *                  desktop's: kde-mimeapps.list on Kubuntu) before
 *                  mimeapps.list; then the old
 *                  defaults.list of the data directories (its defaults only).
 *                  [Default Applications], [Added Associations] and
 *                  [Removed Associations] are kept per file.
 *
 * Plain POSIX, no FreeRTOS, no fmrb allocator (the host tests run it as it
 * is). Every call reads the files again: they are small, and a change made
 * by the user's own desktop (Dolphin's "Open with" and "Remember") takes
 * effect at once.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINUX_MIME_TYPE_MAX 128
#define LINUX_MIME_CHAIN_MAX 12

/* The data directories, most important first: $XDG_DATA_HOME (or
 * ~/.local/share), then $XDG_DATA_DIRS (or /usr/local/share:/usr/share).
 * Returns how many (at most max). */
int linux_xdg_data_dirs(char dirs[][512], int max);

/* The type of the file at path, by its name. False: not known. */
bool linux_mime_type(const char *path, char *out, size_t cap);

/* type and the types it is a kind of, in order (see above). Returns how
 * many (at least 1 for a non-empty type). */
int linux_mime_chain(const char *type, char out[][LINUX_MIME_TYPE_MAX], int max);

/* The mimeapps.list files that exist, in order. */
typedef struct linux_mime_lists linux_mime_lists_t;

#define LINUX_MIME_DEFAULT 0
#define LINUX_MIME_ADDED 1
#define LINUX_MIME_REMOVED 2

/* desktops: the desktop names, colon-separated like XDG_CURRENT_DESKTOP
 * ("family-mruby:kde": ours, then the machine's own desktop's, L15); in each
 * directory the files called <name>-mimeapps.list are read in that order,
 * before mimeapps.list. NULL only when out of memory. */
linux_mime_lists_t *linux_mime_lists_load(const char *desktops);
void linux_mime_lists_free(linux_mime_lists_t *l);
int linux_mime_lists_count(const linux_mime_lists_t *l);
/* The file's path (for the logs and the tests). */
const char *linux_mime_lists_path(const linux_mime_lists_t *l, int file);
/* The value ("a.desktop;b.desktop;") for type in that group of file i, or
 * NULL. */
const char *linux_mime_lists_get(const linux_mime_lists_t *l, int file, int group, const char *type);

#ifdef __cplusplus
}
#endif
