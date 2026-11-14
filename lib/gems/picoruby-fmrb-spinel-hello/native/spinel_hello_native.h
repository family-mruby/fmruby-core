/**
 * @file spinel_hello_native.h
 * @brief Running the Spinel-compiled greeting from a task that is not a Spinel
 *        task -- the minimal example of "Spinel as a gem".
 *
 * An mruby app task creates a Spinel runtime instance of its own, calls the
 * AOT-compiled entry point as if it were a library, and tears the instance
 * down again. begin/end bracket the instance because creating it claims a
 * memory pool; run() calls the entry and returns the string the entry produced.
 *
 * Single owner: there is one instance for the whole firmware, owned by the
 * app task that called begin() until it calls end() or ends. begin() from any
 * other task answers SPINEL_HELLO_BUSY, and the gem's Ruby greets on mruby
 * instead (doc/spinel_multi_instance/report/g1.md).
 */
#ifndef SPINEL_HELLO_NATIVE_H
#define SPINEL_HELLO_NATIVE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** spinel_hello_begin: another app task owns the instance. */
#define SPINEL_HELLO_BUSY (-5)

/** Always 1: this sample is Spinel-only and always compiled in. */
int spinel_hello_available(void);

/** Create the Spinel instance on the calling task, which then owns it. 0 on
 *  success (or if the calling task already owns it), SPINEL_HELLO_BUSY if
 *  another task owns it, other negatives on failure (no memory, or the
 *  instance could not be created). */
int spinel_hello_begin(void);

/** Log that the calling app was given another backend because the instance
 *  is owned elsewhere: which app, which owner, which backend it runs on. */
void spinel_hello_note_fallback(const char *backend);

/** Run the entry for `name` (name_len bytes) and return the greeting. *len_out
 *  gets its byte length. The returned pointer is valid until the next run() or
 *  end(); NULL if not open. */
const char *spinel_hello_run(const char *name, int name_len, int *len_out);

/** Tear the instance down and release its pool. A no-op unless the calling
 *  task owns it. */
void spinel_hello_end(void);

#ifdef __cplusplus
}
#endif

#endif /* SPINEL_HELLO_NATIVE_H */
