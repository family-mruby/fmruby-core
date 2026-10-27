/*
 * fmrb_clipboard.h - the machine's clipboard (doc/linux_native, L12a).
 *
 * One UTF-8 text shared by every Family mruby app: the editor's copy, cut
 * and paste, the Shell's paste, and FmrbApp.clipboard / set_clipboard /
 * clipboard_gen for any other app. At most FMRB_CLIPBOARD_MAX bytes.
 *
 * Where it lives: one fixed buffer, in PSRAM on the devices (no internal RAM
 * at all, the lock's own block included), in ordinary memory on the hosted
 * builds. The generation counts the changes, so an app can tell that the
 * text changed without reading it.
 *
 * Any task may call these; one mutex keeps them apart. Nothing here waits
 * for anything but that mutex.
 *
 * A host clipboard (the Linux PC's, the browser's) is joined through a
 * table the platform installs before the kernel starts:
 *
 *   publish  called with the new text whenever an app sets it (from that
 *            app's task, under the clipboard's lock): the host copies it and
 *            hands it on to its own side later; it must not wait.
 *   take     the host's newest text, when it has one newer than *seq: copy
 *            at most cap bytes into buf, the full length into *len, the new
 *            sequence into *seq, and return true. Asked (under the lock)
 *            each time an app reads the clipboard or its generation, so
 *            text copied in a Linux app is there at the next paste. Must
 *            not wait.
 *
 * Text taken from the host has its line ends made "\n" and NUL bytes
 * dropped. Host text over FMRB_CLIPBOARD_MAX is refused: the clipboard is
 * emptied (a paste then does nothing rather than paste older text), and the
 * log says why.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The largest text the clipboard holds, in bytes. */
#define FMRB_CLIPBOARD_MAX (64 * 1024)

/** A host clipboard to join (see above). Either entry may be NULL. */
typedef struct {
    void (*publish)(const char *text, size_t len);
    bool (*take)(uint32_t *seq, char *buf, size_t cap, size_t *len);
} fmrb_clipboard_host_t;

/** Create the lock. Called once by the kernel before any app runs. */
fmrb_err_t fmrb_clipboard_init(void);

/** A platform's host clipboard, before the kernel starts (NULL: none). */
void fmrb_clipboard_host_install(const fmrb_clipboard_host_t *host);

/** Replace the text. FMRB_ERR_INVALID_PARAM when longer than the maximum
 *  (the clipboard is then unchanged). */
fmrb_err_t fmrb_clipboard_set(const char *text, size_t len);

/** Copy up to cap bytes of the text from byte offset off into buf; returns
 *  the bytes copied (0 past the end). *gen (may be NULL) gets the generation
 *  the bytes belong to, so a reader in pieces can tell a change between
 *  them. */
size_t fmrb_clipboard_read(size_t off, char *buf, size_t cap, uint32_t *gen);

/** The text's length in bytes. */
size_t fmrb_clipboard_length(void);

/** How many times the text has changed since the machine started. */
uint32_t fmrb_clipboard_generation(void);

/** Borrow the text in place (for a large consumer: the editor's paste):
 *  the clipboard stays locked until fmrb_clipboard_unlock. NULL (and no
 *  lock held) when the clipboard is not ready. */
const char *fmrb_clipboard_lock(size_t *len);
void fmrb_clipboard_unlock(void);

#ifdef __cplusplus
}
#endif
