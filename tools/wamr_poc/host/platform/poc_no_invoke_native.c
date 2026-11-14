/*
 * Replaces WAMR's invokeNative (the trampoline that calls a host function
 * through a C signature chosen at run time from the wasm type). It cannot
 * work on wasm, so the PoC registers every host function raw and links this
 * trap instead, on both builds: if any call ever took the typed path, the
 * program would stop here (or, on wasm, at the signature check before it).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
    fprintf(stderr, "invokeNative called: a host function was not raw\n");
    abort();
}
