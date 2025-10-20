#include <stddef.h>
#include <string.h>
#include "tlsf.h"

static tlsf_t prism_tlsf = NULL;
static pool_t prism_pool = NULL;

#define PRISM_POOL_SIZE (256 * 1024)  // 256KB default

static unsigned char prism_memory_pool[PRISM_POOL_SIZE];

int prism_malloc_init(void)
{
    if (prism_tlsf != NULL) {
        return 0;  // Already initialized
    }

    // Create TLSF instance with the memory pool
    prism_tlsf = tlsf_create_with_pool(prism_memory_pool, PRISM_POOL_SIZE);

    if (prism_tlsf == NULL) {
        return -1;  // Initialization failed
    }

    prism_pool = tlsf_get_pool(prism_tlsf);
    return 0;
}

void* prism_malloc(size_t size)
{
    if (prism_tlsf == NULL) {
        prism_malloc_init();
    }
    return tlsf_malloc(prism_tlsf, size);
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

// Optional: Get memory usage statistics
int prism_malloc_check(void)
{
    if (prism_tlsf == NULL) {
        return -1;  // Not initialized
    }
    return tlsf_check(prism_tlsf);
}

// Optional: Get pool statistics
typedef struct {
    size_t total_size;
    size_t used_size;
    size_t free_size;
    int used_blocks;
    int free_blocks;
} prism_pool_stats_t;

static void prism_count_blocks(void* ptr, size_t size, int used, void* user)
{
    prism_pool_stats_t* stats = (prism_pool_stats_t*)user;
    if (used) {
        stats->used_size += size;
        stats->used_blocks++;
    } else {
        stats->free_size += size;
        stats->free_blocks++;
    }
    stats->total_size += size;
}

void prism_get_stats(prism_pool_stats_t* stats)
{
    if (stats == NULL || prism_pool == NULL) {
        return;
    }

    memset(stats, 0, sizeof(prism_pool_stats_t));
    tlsf_walk_pool(prism_pool, prism_count_blocks, stats);
}
