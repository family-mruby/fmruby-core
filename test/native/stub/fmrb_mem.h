// Host-test stand-in for fmrb_mem.h (test/native/file_hal.c): the system
// pool is libc's heap here.
#pragma once

#include <stdlib.h>

#define fmrb_sys_malloc(n) malloc(n)
#define fmrb_sys_calloc(n, s) calloc((n), (s))
#define fmrb_sys_free(p) free(p)
