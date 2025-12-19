#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "tlsf.h"

// Use fmrb_rtos abstraction for mutex
#define MUTEX_TYPE fmrb_semaphore_t
#define MUTEX_INIT(m) ((m) = fmrb_semaphore_create_mutex())
#define MUTEX_LOCK(m) fmrb_semaphore_take((m), FMRB_TICK_MAX)
#define MUTEX_UNLOCK(m) fmrb_semaphore_give((m))
#define MUTEX_DESTROY(m) fmrb_semaphore_delete((m))

static const char *TAG = "fmrb_alloc";

static fmrb_mem_handle_t system_handle = -1;

// Pool management structure
typedef struct fmrb_pool_node {
    fmrb_mem_handle_t handle;
    tlsf_t tlsf;
    pool_t pool;
    MUTEX_TYPE mutex;
    struct fmrb_pool_node *next;
} fmrb_pool_node_t;

// Global pool list
static fmrb_pool_node_t *s_pool_list = NULL;
static MUTEX_TYPE s_list_mutex;
static bool s_list_mutex_initialized = false;
static fmrb_mem_handle_t s_next_handle = 0;

// Initialize global mutex
static void init_list_mutex(void) {
    if (!s_list_mutex_initialized) {
        MUTEX_INIT(s_list_mutex);
        s_list_mutex_initialized = true;
    }
}

// Find pool node by handle
static fmrb_pool_node_t* find_pool_node(fmrb_mem_handle_t handle) {
    fmrb_pool_node_t *node = s_pool_list;
    while (node != NULL) {
        if (node->handle == handle) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

// Create a new memory pool and return its handle
fmrb_mem_handle_t fmrb_malloc_create_handle(void* pool, size_t size) {
    if (pool == NULL || size == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    init_list_mutex();

    // Allocate pool node from the provided pool memory
    // Reserve space at the beginning of the pool for the node structure
    if (size < sizeof(fmrb_pool_node_t) + 1024) {
        ESP_LOGE(TAG, "Pool too small");
        return -1;
    }

    fmrb_pool_node_t *node = (fmrb_pool_node_t*)pool;
    void *tlsf_pool_start = (void*)((char*)pool + sizeof(fmrb_pool_node_t));
    size_t tlsf_pool_size = size - sizeof(fmrb_pool_node_t);

    // Create TLSF instance with the memory pool
    node->tlsf = tlsf_create_with_pool(tlsf_pool_start, tlsf_pool_size);
    if (node->tlsf == NULL) {
        ESP_LOGE(TAG, "tlsf_create_with_pool failed");
        return -1;
    }

    node->pool = tlsf_get_pool(node->tlsf);

    // Initialize mutex for this pool
    MUTEX_INIT(node->mutex);

    // Assign handle and add to list
    MUTEX_LOCK(s_list_mutex);
    node->handle = s_next_handle++;
    node->next = s_pool_list;
    s_pool_list = node;
    MUTEX_UNLOCK(s_list_mutex);

    ESP_LOGI(TAG, "Created pool handle=%d, size=%zu", node->handle, tlsf_pool_size);
    return node->handle;
}

// Destroy a memory pool
int fmrb_malloc_destroy_handle(fmrb_mem_handle_t handle) {
    init_list_mutex();

    MUTEX_LOCK(s_list_mutex);

    fmrb_pool_node_t *prev = NULL;
    fmrb_pool_node_t *node = s_pool_list;

    while (node != NULL) {
        if (node->handle == handle) {
            // Remove from list
            if (prev == NULL) {
                s_pool_list = node->next;
            } else {
                prev->next = node->next;
            }

            MUTEX_UNLOCK(s_list_mutex);

            // Destroy TLSF and mutex
            tlsf_destroy(node->tlsf);
            MUTEX_DESTROY(node->mutex);

            // Note: We don't free the node itself as it's part of the original pool memory
            ESP_LOGI(TAG, "Destroyed pool handle=%d", handle);
            return 0;
        }
        prev = node;
        node = node->next;
    }

    MUTEX_UNLOCK(s_list_mutex);
    ESP_LOGE(TAG, "Pool handle not found: %d", handle);
    return -1;
}

// Allocate memory from a pool
void* fmrb_malloc(fmrb_mem_handle_t handle, size_t size) {
    init_list_mutex();

    MUTEX_LOCK(s_list_mutex);
    fmrb_pool_node_t *node = find_pool_node(handle);
    MUTEX_UNLOCK(s_list_mutex);

    if (node == NULL) {
        ESP_LOGE(TAG, "Pool handle not found: %d", handle);
        return NULL;
    }

    MUTEX_LOCK(node->mutex);
    void *ptr = tlsf_malloc(node->tlsf, size);
    MUTEX_UNLOCK(node->mutex);

    return ptr;
}

// Calloc from a pool
void* fmrb_calloc(fmrb_mem_handle_t handle, size_t nmemb, size_t size) {
    init_list_mutex();

    MUTEX_LOCK(s_list_mutex);
    fmrb_pool_node_t *node = find_pool_node(handle);
    MUTEX_UNLOCK(s_list_mutex);

    if (node == NULL) {
        ESP_LOGE(TAG, "Pool handle not found: %d", handle);
        return NULL;
    }

    size_t total_size = nmemb * size;

    MUTEX_LOCK(node->mutex);
    void* ptr = tlsf_malloc(node->tlsf, total_size);
    if (ptr != NULL) {
        memset(ptr, 0, total_size);
    }
    MUTEX_UNLOCK(node->mutex);

    return ptr;
}

// Realloc from a pool
void* fmrb_realloc(fmrb_mem_handle_t handle, void* ptr, size_t size) {
    init_list_mutex();

    MUTEX_LOCK(s_list_mutex);
    fmrb_pool_node_t *node = find_pool_node(handle);
    MUTEX_UNLOCK(s_list_mutex);

    if (node == NULL) {
        ESP_LOGE(TAG, "Pool handle not found: %d", handle);
        return NULL;
    }

    MUTEX_LOCK(node->mutex);
    void *new_ptr = tlsf_realloc(node->tlsf, ptr, size);
    MUTEX_UNLOCK(node->mutex);

    return new_ptr;
}

// Free memory from a pool
void fmrb_free(fmrb_mem_handle_t handle, void* ptr) {
    if (ptr == NULL) {
        return;
    }

    init_list_mutex();

    MUTEX_LOCK(s_list_mutex);
    fmrb_pool_node_t *node = find_pool_node(handle);
    MUTEX_UNLOCK(s_list_mutex);

    if (node == NULL) {
        ESP_LOGE(TAG, "Pool handle not found: %d", handle);
        return;
    }

    MUTEX_LOCK(node->mutex);
    tlsf_free(node->tlsf, ptr);
    MUTEX_UNLOCK(node->mutex);
}

// Check integrity of a pool
int32_t fmrb_malloc_check(fmrb_mem_handle_t handle) {
    init_list_mutex();

    MUTEX_LOCK(s_list_mutex);
    fmrb_pool_node_t *node = find_pool_node(handle);
    MUTEX_UNLOCK(s_list_mutex);

    if (node == NULL) {
        ESP_LOGE(TAG, "Pool handle not found: %d", handle);
        return -1;
    }

    MUTEX_LOCK(node->mutex);
    int32_t result = tlsf_check(node->tlsf);
    MUTEX_UNLOCK(node->mutex);

    return result;
}

// Block counting callback
static void fmrb_count_blocks(void* ptr, size_t size, int used, void* user) {
    fmrb_pool_stats_t* stats = (fmrb_pool_stats_t*)user;
    int32_t used_i32 = (int32_t)used;
    if (used_i32) {
        stats->used_size += size;
        stats->used_blocks++;
    } else {
        stats->free_size += size;
        stats->free_blocks++;
    }
    stats->total_size += size;
}

// Get statistics for a pool
int fmrb_get_stats(fmrb_mem_handle_t handle, fmrb_pool_stats_t* stats) {
    if (stats == NULL) {
        return -1;
    }

    init_list_mutex();

    MUTEX_LOCK(s_list_mutex);
    fmrb_pool_node_t *node = find_pool_node(handle);
    MUTEX_UNLOCK(s_list_mutex);

    if (node == NULL || node->pool == NULL) {
        ESP_LOGE(TAG, "Pool handle not found: %d", handle);
        return -1;
    }

    MUTEX_LOCK(node->mutex);
    memset(stats, 0, sizeof(fmrb_pool_stats_t));
    tlsf_walk_pool(node->pool, fmrb_count_blocks, stats);
    MUTEX_UNLOCK(node->mutex);

    return 0;
}

void fmrb_init_system_mem(void){
    static bool initialized = false;
    if(initialized == true)
    {
        return;
    }
    initialized = true;

    system_handle = fmrb_malloc_create_handle(
    fmrb_get_mempool_ptr(POOL_ID_SYSTEM),
    FMRB_MEM_POOL_SIZE_SYSTEM);
    ESP_LOGI(TAG, "System mem allocator initialized. Handle = %d", system_handle);
}

void* fmrb_sys_malloc(size_t size)
{
    return fmrb_malloc(system_handle, size);
}

void fmrb_sys_free(void* ptr)
{
    fmrb_free(system_handle, ptr);
}

