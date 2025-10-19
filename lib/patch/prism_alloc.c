#include <stddef.h>
#include <string.h>
#include <stdlib.h>

// Fallback to standard malloc for host builds (picorbc)
// This file provides prism_* allocator functions when building
// the compiler (picorbc) on the host machine.

int prism_malloc_init(void)
{
    return 0;  // No initialization needed for host builds
}

void* prism_malloc(size_t size)
{
    return malloc(size);
}

void* prism_calloc(size_t nmemb, size_t size)
{
    return calloc(nmemb, size);
}

void* prism_realloc(void* ptr, size_t size)
{
    return realloc(ptr, size);
}

void prism_free(void* ptr)
{
    free(ptr);
}
