#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "fmrb_mem_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pool statistics structure
typedef struct {
    size_t total_size;
    size_t used_size;
    size_t free_size;
    int32_t used_blocks;
    int32_t free_blocks;
} fmrb_pool_stats_t;

// Memory pool management functions
void* fmrb_get_mempool_ptr(int32_t id);
size_t fmrb_get_mempool_size(int32_t id);
void* fmrb_get_mempool_app_ptr(int32_t no);

// Memory pool debugging functions
void fmrb_mempool_print_ranges(void);
void fmrb_mempool_check_pointer(const void* ptr);

// Pool management (thread-safe)
void fmrb_mem_init(void);
fmrb_mem_handle_t fmrb_mem_create_handle(void* pool, size_t size, enum FMRB_MEM_POOL_ID mem_pool_id);
int fmrb_mem_destroy_handle(fmrb_mem_handle_t handle);

// Memory allocation functions
void* fmrb_malloc(fmrb_mem_handle_t handle, size_t size);
void* fmrb_calloc(fmrb_mem_handle_t handle, size_t nmemb, size_t size);
void* fmrb_realloc(fmrb_mem_handle_t handle, void* ptr, size_t size);
void fmrb_free(fmrb_mem_handle_t handle, void* ptr);

// Pool operations
int fmrb_mem_handle_exist(enum FMRB_MEM_POOL_ID mem_pool_id);
int32_t fmrb_mem_check(fmrb_mem_handle_t handle);
int fmrb_mem_get_stats(fmrb_mem_handle_t handle, fmrb_pool_stats_t* stats);

// malloc for system functions
void* fmrb_sys_malloc(size_t size);
void fmrb_sys_free(void* ptr);
int fmrb_sys_mem_get_stats(fmrb_pool_stats_t* stats);

// Platform PSRAM information (ESP32 only)
void fmrb_mem_print_psram_info(void);

#ifdef __cplusplus
}
#endif
