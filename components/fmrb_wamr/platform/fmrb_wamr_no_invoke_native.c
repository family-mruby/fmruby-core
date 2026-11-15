/*
 * Stands in for WAMR's invokeNative, the trampoline that calls a host
 * function through a C signature chosen at run time from the wasm type. It
 * cannot work on the browser build (emscripten), so every host function is
 * registered raw (the generated fmrb_boundary_wasm.inc) and this trap is
 * linked on every build instead (report/w1a.md 3).
 */
#include <stdint.h>
#include <stdlib.h>
#include "fmrb_log.h"

typedef void (*GenericFunctionPointer)(void);

#if UINTPTR_MAX == UINT32_MAX
void
invokeNative(GenericFunctionPointer f, uint32_t *args, uint32_t n_stacks)
#else
void
invokeNative(GenericFunctionPointer f, uint64_t *args, uint64_t n_stacks)
#endif
{
    (void)f;
    (void)args;
    (void)n_stacks;
    FMRB_LOGE("wamr", "invokeNative called: a host function was not registered raw");
    abort();
}
