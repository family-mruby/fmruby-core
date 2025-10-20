#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "tlsf.h"

// TLSF-based prism allocator for picorbc (host build)
// This file is copied to mruby-compiler2/lib/ during rake setup

static tlsf_t prism_tlsf = NULL;
static pool_t prism_pool = NULL;

#ifndef PRISM_POOL_SIZE
#define PRISM_POOL_SIZE (1 * 1024 * 1024)  // 1MB default (256KB was too small)
#endif

// Use static to ensure it's in data segment, not stack
static unsigned char prism_memory_pool[PRISM_POOL_SIZE] __attribute__((aligned(8)));

int prism_malloc_init(void)
{
    if (prism_tlsf != NULL) {
        return 0;  // Already initialized
    }

    fprintf(stderr, "[PRISM] Initializing TLSF with %d KB pool...\n", PRISM_POOL_SIZE / 1024);

    // Create TLSF instance with the memory pool
    prism_tlsf = tlsf_create_with_pool(prism_memory_pool, PRISM_POOL_SIZE);

    if (prism_tlsf == NULL) {
        fprintf(stderr, "[PRISM] TLSF Init Failed!\n");
        return -1;  // Initialization failed
    }

    prism_pool = tlsf_get_pool(prism_tlsf);
    fprintf(stderr, "[PRISM] TLSF initialized successfully\n");
    return 0;
}

void* prism_malloc(size_t size)
{
    if (prism_tlsf == NULL) {
        if (prism_malloc_init() != 0) {
            fprintf(stderr, "[PRISM] malloc init failed for size %zu\n", size);
            return NULL;
        }
    }
    void* ptr = tlsf_malloc(prism_tlsf, size);
    if (ptr == NULL) {
        fprintf(stderr, "[PRISM] Failed to allocate %zu bytes\n", size);
    }
    return ptr;
}

void* prism_calloc(size_t nmemb, size_t size)
{
    if (prism_tlsf == NULL) {
        prism_malloc_init();
    }

    size_t total_size = nmemb * size;
    void* ptr = tlsf_malloc(prism_tlsf, total_size);

    if (ptr != NULL) {
        memset(ptr, 0, total_size);
    }

    return ptr;
}

void* prism_realloc(void* ptr, size_t size)
{
    if (prism_tlsf == NULL) {
        prism_malloc_init();
    }
    return tlsf_realloc(prism_tlsf, ptr, size);
}

void prism_free(void* ptr)
{
    if (ptr == NULL) {
        return;  // C99: free(NULL) is allowed
    }

    if (prism_tlsf == NULL) {
        return;  // Not initialized, nothing to free
    }

    tlsf_free(prism_tlsf, ptr);
}
