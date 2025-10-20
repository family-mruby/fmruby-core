#include <stddef.h>
#include <string.h>
#include "tlsf.h"
#include "fmrb_mem.h"

static tlsf_t fmrb_tlsf = NULL;
static pool_t fmrb_pool = NULL;

#define FMRB_POOL_SIZE (256 * 1024)  // 256KB default

static unsigned char fmrb_memory_pool[FMRB_POOL_SIZE];

int fmrb_malloc_init(void)
{
    if (fmrb_tlsf != NULL) {
        return 0;  // Already initialized
    }

    // Create TLSF instance with the memory pool
    fmrb_tlsf = tlsf_create_with_pool(fmrb_memory_pool, FMRB_POOL_SIZE);

    if (fmrb_tlsf == NULL) {
        return -1;  // Initialization failed
    }

    fmrb_pool = tlsf_get_pool(fmrb_tlsf);
    return 0;
}

void* fmrb_malloc(size_t size)
{
    if (fmrb_tlsf == NULL) {
        fmrb_malloc_init();
    }
    return tlsf_malloc(fmrb_tlsf, size);
}

void* fmrb_calloc(size_t nmemb, size_t size)
{
    if (fmrb_tlsf == NULL) {
        fmrb_malloc_init();
    }

    size_t total_size = nmemb * size;
    void* ptr = tlsf_malloc(fmrb_tlsf, total_size);

    if (ptr != NULL) {
        memset(ptr, 0, total_size);
    }

    return ptr;
}

void* fmrb_realloc(void* ptr, size_t size)
{
    if (fmrb_tlsf == NULL) {
        fmrb_malloc_init();
    }
    return tlsf_realloc(fmrb_tlsf, ptr, size);
}

void fmrb_free(void* ptr)
{
    if (ptr == NULL) {
        return;  // C99: free(NULL) is allowed
    }

    if (fmrb_tlsf == NULL) {
        return;  // Not initialized, nothing to free
    }

    tlsf_free(fmrb_tlsf, ptr);
}

// Optional: Get memory usage statistics
int fmrb_malloc_check(void)
{
    if (fmrb_tlsf == NULL) {
        return -1;  // Not initialized
    }
    return tlsf_check(fmrb_tlsf);
}

// Optional: Get pool statistics
typedef struct {
    size_t total_size;
    size_t used_size;
    size_t free_size;
    int used_blocks;
    int free_blocks;
} fmrb_pool_stats_t;

static void fmrb_count_blocks(void* ptr, size_t size, int used, void* user)
{
    fmrb_pool_stats_t* stats = (fmrb_pool_stats_t*)user;
    if (used) {
        stats->used_size += size;
        stats->used_blocks++;
    } else {
        stats->free_size += size;
        stats->free_blocks++;
    }
    stats->total_size += size;
}

void fmrb_get_stats(fmrb_pool_stats_t* stats)
{
    if (stats == NULL || fmrb_pool == NULL) {
        return;
    }

    memset(stats, 0, sizeof(fmrb_pool_stats_t));
    tlsf_walk_pool(fmrb_pool, fmrb_count_blocks, stats);
}
