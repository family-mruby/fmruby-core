/*
 * native_clip.h - the machine's clipboard joined to the Linux one
 * (doc/linux_native, L12a). Plain C, no FreeRTOS, no output library.
 *
 * Two hand-overs between the machine's tasks and the program's main thread,
 * where the output (the compositor's Wayland selection, the SDL window's
 * clipboard) lives:
 *
 *   out  Family mruby -> Linux: the kernel's clipboard calls publish
 *        (fmrb_clipboard.h) from the task that set it; the text is copied
 *        here and the main thread woken; the output takes it
 *        (native_clip_take_out) and offers it to Linux.
 *   in   Linux -> Family mruby: the output reads Linux's text on the main
 *        thread, without waiting, and hands it here (native_clip_offer);
 *        the kernel takes it the next time an app reads the clipboard.
 *
 * Each side holds the lock only to copy. The buffers are static (two times
 * FMRB_CLIPBOARD_MAX), so a task never calls malloc here.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Join the kernel's clipboard to this one; wake (may be NULL) is called
 * after each new text from Family mruby, from the task that set it. Main
 * thread, before the machine starts. */
void native_clip_install(void (*wake)(void));
bool native_clip_installed(void);

/* Main thread: Family mruby's newest text since the last take, as a
 * malloc'd copy (NUL-terminated; free it). */
bool native_clip_take_out(char **text, size_t *len);

/* Main thread: Linux's new text. len over FMRB_CLIPBOARD_MAX hands over
 * only the length (text may be NULL): the kernel refuses it. */
void native_clip_offer(const char *text, size_t len);

/* Counters for the control socket and the log. */
typedef struct {
    unsigned out;       /* texts Family mruby published */
    unsigned in;        /* texts offered from Linux */
    unsigned in_big;    /* ...of which over the maximum */
    unsigned taken;     /* texts the kernel took */
    size_t last_in;     /* the length of the last offered */
    size_t last_out;    /* the length of the last published */
} native_clip_stats_t;
void native_clip_stats(native_clip_stats_t *out);

#ifdef __cplusplus
}
#endif
