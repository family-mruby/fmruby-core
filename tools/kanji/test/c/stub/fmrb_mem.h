// Host-test stand-in for fmrb_mem.h (tools/kanji/test/c_lookup.rb): the
// SYSTEM pool is libc's heap, with the live bytes counted so the test can
// report what an open dictionary holds and catch leaks.
#pragma once

#include <stddef.h>

void *fmrb_sys_malloc(size_t size);
void fmrb_sys_free(void *ptr);
