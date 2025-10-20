#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "tlsf.h"

// TLSF-based prism allocator for mruby-compiler2
// This file is copied to mruby-compiler2/lib/ during rake setup
// Used by both:
//   - Host build (picorbc): PRISM_BUILD_HOST defined, 288KB pool
//   - Target build (ESP32/Linux eval): 64KB pool

static tlsf_t prism_tlsf = NULL;
static pool_t prism_pool = NULL;

#ifndef PRISM_POOL_SIZE
  #ifdef PRISM_BUILD_HOST
    // Host build (picorbc): needs ~220KB peak for compiling mrblib
    #define PRISM_POOL_SIZE (288 * 1024)  // 288KB with safety margin
  #else
    // Target build (ESP32/Linux eval): much smaller user code
    #define PRISM_POOL_SIZE (64 * 1024)   // 64KB initial - will measure actual usage
  #endif
#endif

// Use static to ensure it's in data segment, not stack
static unsigned char prism_memory_pool[PRISM_POOL_SIZE] __attribute__((aligned(8)));

static size_t total_allocated = 0;
static size_t peak_allocated = 0;
static size_t allocation_count = 0;

int prism_malloc_init(void)
{
    if (prism_tlsf != NULL) {
        return 0;  // Already initialized
    }

    #ifdef PRISM_BUILD_HOST
    fprintf(stderr, "[PRISM-HOST] Initializing TLSF with %d KB pool...\n", PRISM_POOL_SIZE / 1024);
    #else
    fprintf(stderr, "[PRISM-TARGET] Initializing TLSF with %d KB pool...\n", PRISM_POOL_SIZE / 1024);
    #endif
    fprintf(stderr, "[PRISM] TLSF overhead size: %zu bytes\n", tlsf_size());
    fprintf(stderr, "[PRISM] TLSF pool overhead: %zu bytes\n", tlsf_pool_overhead());
    fprintf(stderr, "[PRISM] TLSF alloc overhead: %zu bytes per allocation\n", tlsf_alloc_overhead());
    fprintf(stderr, "[PRISM] TLSF block size min: %zu bytes\n", tlsf_block_size_min());
    fprintf(stderr, "[PRISM] TLSF block size max: %zu bytes\n", tlsf_block_size_max());

    // Create TLSF instance with the memory pool
    prism_tlsf = tlsf_create_with_pool(prism_memory_pool, PRISM_POOL_SIZE);

    if (prism_tlsf == NULL) {
        fprintf(stderr, "[PRISM] TLSF Init Failed!\n");
        return -1;  // Initialization failed
    }

    prism_pool = tlsf_get_pool(prism_tlsf);

    size_t actual_overhead = tlsf_size() + tlsf_pool_overhead();
    size_t usable_size = PRISM_POOL_SIZE - actual_overhead;

    fprintf(stderr, "[PRISM] TLSF initialized successfully\n");
    fprintf(stderr, "[PRISM] Actual overhead: %zu bytes, Usable: %zu bytes (%.1f%%)\n",
            actual_overhead, usable_size, (usable_size * 100.0) / PRISM_POOL_SIZE);

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
        fprintf(stderr, "[PRISM] Failed to allocate %zu bytes (total allocated: %zu, peak: %zu, count: %zu)\n",
                size, total_allocated, peak_allocated, allocation_count);
    } else {
        total_allocated += size;
        allocation_count++;
        if (total_allocated > peak_allocated) {
            peak_allocated = total_allocated;
        }
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
        total_allocated += total_size;
        allocation_count++;
        if (total_allocated > peak_allocated) {
            peak_allocated = total_allocated;
        }
    } else {
        fprintf(stderr, "[PRISM] calloc failed: %zu x %zu = %zu bytes (total allocated: %zu, peak: %zu, count: %zu)\n",
                nmemb, size, total_size, total_allocated, peak_allocated, allocation_count);
    }

    return ptr;
}

void* prism_realloc(void* ptr, size_t size)
{
    if (prism_tlsf == NULL) {
        prism_malloc_init();
    }

    size_t old_size = 0;
    if (ptr != NULL) {
        old_size = tlsf_block_size(ptr);
    }

    void* new_ptr = tlsf_realloc(prism_tlsf, ptr, size);

    if (new_ptr != NULL) {
        total_allocated = total_allocated - old_size + size;
        if (total_allocated > peak_allocated) {
            peak_allocated = total_allocated;
        }
    }

    return new_ptr;
}

void prism_free(void* ptr)
{
    if (ptr == NULL) {
        return;  // C99: free(NULL) is allowed
    }

    if (prism_tlsf == NULL) {
        return;  // Not initialized, nothing to free
    }

    size_t block_size = tlsf_block_size(ptr);
    total_allocated -= block_size;

    tlsf_free(prism_tlsf, ptr);
}

// Report peak memory usage (for debugging)
void prism_malloc_stats(void)
{
    fprintf(stderr, "[PRISM] Memory stats: peak=%zu bytes (%.1f KB), allocations=%zu\n",
            peak_allocated, peak_allocated / 1024.0, allocation_count);
}

// Cleanup function to reset allocator (called when picorbc exits)
void prism_malloc_cleanup(void)
{
    if (prism_tlsf != NULL) {
        fprintf(stderr, "[PRISM] Cleanup: peak usage was %zu bytes (%.1f KB) across %zu allocations\n",
                peak_allocated, peak_allocated / 1024.0, allocation_count);
    }
}
