/**
 * @file spinel_hello_native.h
 * @brief Running the Spinel-compiled greeting from a task that is not a Spinel
 *        task -- the minimal example of "Spinel as a gem".
 *
 * An mruby app task creates a Spinel runtime instance of its own, calls the
 * AOT-compiled entry point as if it were a library, and tears the instance
 * down again. begin/end bracket the instance because creating it claims a
 * memory pool; run() calls the entry and returns the string the entry produced.
 */
#ifndef SPINEL_HELLO_NATIVE_H
#define SPINEL_HELLO_NATIVE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Always 1: this sample is Spinel-only and always compiled in. */
int spinel_hello_available(void);

/** Create the Spinel instance on the calling task. 0 on success, negative on
 *  failure (no memory, or the instance could not be created). */
int spinel_hello_begin(void);

/** Run the entry and return the greeting. *len_out gets its byte length. The
 *  returned pointer is valid until the next run() or end(); NULL if not open. */
const char *spinel_hello_run(int *len_out);

/** Tear the instance down and release its pool. */
void spinel_hello_end(void);

#ifdef __cplusplus
}
#endif

#endif /* SPINEL_HELLO_NATIVE_H */
