/*
 * linux_pty.h - one Linux program under a pseudo terminal (doc/linux_native,
 * L10): start it, read what it prints, write what is typed, send it a
 * signal, learn that it ended and with which code, and end it.
 *
 * Plain POSIX and nothing else (no FreeRTOS, no fmrb allocator, no log), so
 * the host tests (test/native/pty.c) run it as it is. The OS services table
 * (platform_sys_linux.c) puts it behind fmrb_platform_sys_t's proc_* entries
 * and turns the Family mruby working directory into a real one.
 *
 * Nothing here waits: the terminal is read and written without blocking,
 * the end of the program is learnt with waitpid(WNOHANG), and ending a
 * program that is still running is left to a detached helper thread (see
 * linux_pty_close). The cooperative machine never stops for a program.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fmrb_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Programs that may run at once (all apps together). */
#define LINUX_PTY_MAX 8

typedef struct {
    /* Run with /bin/sh -c. NULL: the interactive shell (bash, or sh when
     * there is no bash). */
    const char *command;
    /* The real working directory; must be a directory. */
    const char *cwd;
    /* The terminal's size (TIOCSWINSZ, and COLUMNS / LINES). */
    uint16_t cols;
    uint16_t rows;
    /* Whose program it is: linux_pty_close_owned(owner) ends it. */
    void *owner;
} linux_pty_spec_t;

/* Start a program. *id is a positive handle. FMRB_ERR_NO_RESOURCE when
 * LINUX_PTY_MAX are running, FMRB_ERR_NOT_FOUND when cwd is not a directory,
 * FMRB_ERR_FAILED when the terminal or the process could not be made (why
 * says what, when not NULL). */
fmrb_err_t linux_pty_start(const linux_pty_spec_t *spec, int32_t *id, char *why, size_t why_cap);

/* What the program printed since the last read. > 0: bytes in buf; 0:
 * nothing just now; -1: no more will come (every holder of the terminal
 * has closed it), or no such program. */
int32_t linux_pty_read(int32_t id, void *buf, size_t cap);

/* Hand bytes to the program's input. Returns the bytes taken (0 when the
 * terminal's input is full just now), -1 when it cannot take any more. */
int32_t linux_pty_write(int32_t id, const void *buf, size_t len);

/* Send sig (SIGINT, SIGTERM, ...) to the terminal's foreground process
 * group: the running command, not a shell that waits for it. */
fmrb_err_t linux_pty_signal(int32_t id, int sig);

/* true once the program has ended; *code is its exit status, or 128 + the
 * signal that ended it. */
bool linux_pty_exited(int32_t id, int *code);

/* Tell the program its terminal has a new size (SIGWINCH). */
fmrb_err_t linux_pty_resize(int32_t id, uint16_t cols, uint16_t rows);

/* Forget a program. One still running is asked to end (SIGHUP to its
 * session, the terminal closed), and a helper thread ends what is left of
 * the session after a moment (SIGKILL) and reaps it: nothing is left behind,
 * no zombie and no orphan, and the caller does not wait. */
void linux_pty_close(int32_t id);

/* linux_pty_close for each program of owner (an app ending, or its task
 * deleted). Returns how many. */
int linux_pty_close_owned(void *owner);

/* The program is ending: every session gets SIGKILL now. */
void linux_pty_close_all(void);

/* Programs started and not closed (diagnostics, tests). */
int linux_pty_count(void);

#ifdef __cplusplus
}
#endif
