#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fmrb_rtos.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_err.h"
#include "tlsf.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_heap_caps.h"
#endif

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
    enum FMRB_MEM_POOL_ID mem_pool_id;
    tlsf_t tlsf;
    pool_t pool;
    MUTEX_TYPE mutex;
    struct fmrb_pool_node *next;
} fmrb_pool_node_t;

// Global pool list
static fmrb_pool_node_t *g_pool_list = NULL;
static MUTEX_TYPE g_list_mutex;
static bool g_list_mutex_initialized = false;
static fmrb_mem_handle_t g_next_handle = 1;

// Initialize global mutex
static void init_list_mutex(void) {
    if (!g_list_mutex_initialized) {
        MUTEX_INIT(g_list_mutex);
        g_list_mutex_initialized = true;
    }
}

// Find pool node by handle
static fmrb_pool_node_t* find_pool_node(fmrb_mem_handle_t handle) {
    fmrb_pool_node_t *node = g_pool_list;
    while (node != NULL) {
        if (node->handle == handle) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

// Create a new memory pool and return its handle
fmrb_mem_handle_t fmrb_mem_create_handle(void* pool, size_t size, enum FMRB_MEM_POOL_ID mem_pool_id) {
    if (pool == NULL || size == 0) {
        FMRB_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    // Allocate pool node from the provided pool memory
    // Reserve space at the beginning of the pool for the node structure
    if (size < sizeof(fmrb_pool_node_t) + 1024) {
        FMRB_LOGE(TAG, "Pool too small");
        return -1;
    }

    fmrb_pool_node_t *node = (fmrb_pool_node_t*)pool;
    node->mem_pool_id = mem_pool_id;
    void *tlsf_pool_start = (void*)((char*)pool + sizeof(fmrb_pool_node_t));
    size_t tlsf_pool_size = size - sizeof(fmrb_pool_node_t);

    // Create TLSF instance with the memory pool
    node->tlsf = tlsf_create_with_pool(tlsf_pool_start, tlsf_pool_size);
    if (node->tlsf == NULL) {
        FMRB_LOGE(TAG, "tlsf_create_with_pool failed");
        return -1;
    }

    node->pool = tlsf_get_pool(node->tlsf);

    // Initialize mutex for this pool
    MUTEX_INIT(node->mutex);

    // Assign handle and add to list
    MUTEX_LOCK(g_list_mutex);
    node->handle = g_next_handle;
    g_next_handle++;
    node->next = g_pool_list;
    g_pool_list = node;
    MUTEX_UNLOCK(g_list_mutex);

    FMRB_LOGI(TAG, "Created pool handle=%d, size=%zu", node->handle, tlsf_pool_size);
    return node->handle;
}

// Destroy a memory pool
int fmrb_mem_destroy_handle(fmrb_mem_handle_t handle) {
    MUTEX_LOCK(g_list_mutex);

    fmrb_pool_node_t *prev = NULL;
    fmrb_pool_node_t *node = g_pool_list;

    while (node != NULL) {
        if (node->handle == handle) {
            // Remove from list
            if (prev == NULL) {
                g_pool_list = node->next;
            } else {
                prev->next = node->next;
            }

            MUTEX_UNLOCK(g_list_mutex);

            // Destroy TLSF and mutex
            tlsf_destroy(node->tlsf);
            MUTEX_DESTROY(node->mutex);

            // Note: We don't free the node itself as it's part of the original pool memory
            FMRB_LOGI(TAG, "Destroyed pool handle=%d", handle);
            return 0;
        }
        prev = node;
        node = node->next;
    }

    MUTEX_UNLOCK(g_list_mutex);
    FMRB_LOGE(TAG, "Pool handle not found: %d", handle);
    return -1;
}

// Allocate memory from a pool
void* fmrb_malloc(fmrb_mem_handle_t handle, size_t size) {
    MUTEX_LOCK(g_list_mutex);
    fmrb_pool_node_t *node = find_pool_node(handle);
    MUTEX_UNLOCK(g_list_mutex);

    if (node == NULL) {
        FMRB_LOGE(TAG, "Pool handle not found: %d", handle);
        return NULL;
    }

    MUTEX_LOCK(node->mutex);
    void *ptr = tlsf_malloc(node->tlsf, size);
    MUTEX_UNLOCK(node->mutex);

    return ptr;
}

// Calloc from a pool
void* fmrb_calloc(fmrb_mem_handle_t handle, size_t nmemb, size_t size) {
    MUTEX_LOCK(g_list_mutex);
    fmrb_pool_node_t *node = find_pool_node(handle);
    MUTEX_UNLOCK(g_list_mutex);

    if (node == NULL) {
        FMRB_LOGE(TAG, "Pool handle not found: %d", handle);
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
    MUTEX_LOCK(g_list_mutex);
    fmrb_pool_node_t *node = find_pool_node(handle);
    MUTEX_UNLOCK(g_list_mutex);

    if (node == NULL) {
        FMRB_LOGE(TAG, "Pool handle not found: %d", handle);
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

    MUTEX_LOCK(g_list_mutex);
    fmrb_pool_node_t *node = find_pool_node(handle);
    MUTEX_UNLOCK(g_list_mutex);

    if (node == NULL) {
        FMRB_LOGE(TAG, "Pool handle not found: %d", handle);
        return;
    }

    MUTEX_LOCK(node->mutex);
    tlsf_free(node->tlsf, ptr);
    MUTEX_UNLOCK(node->mutex);
}

// Check integrity of a pool
int32_t fmrb_mem_check(fmrb_mem_handle_t handle) {
    MUTEX_LOCK(g_list_mutex);
    fmrb_pool_node_t *node = find_pool_node(handle);
    MUTEX_UNLOCK(g_list_mutex);

    if (node == NULL) {
        FMRB_LOGE(TAG, "Pool handle not found: %d", handle);
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

int fmrb_mem_handle_exist(enum FMRB_MEM_POOL_ID id){
    MUTEX_LOCK(g_list_mutex);
    fmrb_pool_node_t *node = g_pool_list;
    while (node != NULL) {
        if (node->mem_pool_id == id) {
            MUTEX_UNLOCK(g_list_mutex);
            return 1;  // Found
        }
        node = node->next;
    }
    MUTEX_UNLOCK(g_list_mutex);
    return 0;  // Not found
}

// Get statistics for a pool
int fmrb_mem_get_stats(fmrb_mem_handle_t handle, fmrb_pool_stats_t* stats) {
    if (stats == NULL) {
        return -1;
    }
    MUTEX_LOCK(g_list_mutex);
    fmrb_pool_node_t *node = find_pool_node(handle);
    MUTEX_UNLOCK(g_list_mutex);

    if (node == NULL || node->pool == NULL) {
        FMRB_LOGE(TAG, "Pool handle not found: %d", handle);
        return -1;
    }

    MUTEX_LOCK(node->mutex);
    memset(stats, 0, sizeof(fmrb_pool_stats_t));
    tlsf_walk_pool(node->pool, fmrb_count_blocks, stats);
    MUTEX_UNLOCK(node->mutex);

    return 0;
}

static void fmrb_sys_mem_init(void){
    system_handle = fmrb_mem_create_handle(fmrb_get_mempool_ptr(POOL_ID_SYSTEM), FMRB_MEM_POOL_SIZE_SYSTEM, POOL_ID_SYSTEM);
    FMRB_LOGI(TAG, "System mem allocator initialized. Handle = %d", system_handle);
}

void* fmrb_sys_malloc(size_t size)
{
    return fmrb_malloc(system_handle, size);
}

void fmrb_sys_free(void* ptr)
{
    fmrb_free(system_handle, ptr);
}

void fmrb_mem_init(void){
    static bool initialized = false;
    if(initialized == true)
    {
        return;
    }
    initialized = true;

    init_list_mutex();
    fmrb_sys_mem_init();
}

// Get statistics for system pool
int fmrb_sys_mem_get_stats(fmrb_pool_stats_t* stats)
{
    if (system_handle < 0) {
        FMRB_LOGE(TAG, "System pool not initialized");
        return -1;
    }
    return fmrb_mem_get_stats(system_handle, stats);
}

// Print PSRAM information (ESP32 only)
void fmrb_mem_print_psram_info(void)
{
#ifndef CONFIG_IDF_TARGET_LINUX
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    if (total_psram > 0) {
        size_t used_psram = total_psram - free_psram;
        FMRB_LOGI(TAG, "PSRAM Total: %zu KB", total_psram / 1024);
        FMRB_LOGI(TAG, "PSRAM Used:  %zu KB (%zu%%)",
                  used_psram / 1024,
                  (used_psram * 100) / total_psram);
        FMRB_LOGI(TAG, "PSRAM Free:  %zu KB (%zu%%)",
                  free_psram / 1024,
                  (free_psram * 100) / total_psram);
    } else {
        FMRB_LOGI(TAG, "PSRAM: Not available");
    }
#endif
}
