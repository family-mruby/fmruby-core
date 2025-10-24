#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum FMRB_MEM_POOL_ID{
    POOL_ID_KERNEL = 0,
    POOL_ID_SYSTEM_APP,
    POOL_ID_USER_APP0,
    POOL_ID_USER_APP1,
    POOL_ID_USER_APP2,
    POOL_ID_MAX
};

typedef int8_t fmrb_mem_handle_t;

#ifndef PRISM_POOL_SIZE
  #ifdef PRISM_BUILD_HOST
    // Host build (picorbc): needs ~220KB peak for compiling mrblib
    #define FMRB_MEM_PRISM_POOL_SIZE (288 * 1024)  // 288KB with safety margin
  #else
    // Target build (ESP32/Linux eval): much smaller user code
    #define FMRB_MEM_PRISM_POOL_SIZE (64 * 1024)   // 64KB initial - will measure actual usage
  #endif
#endif

//SRAM
#define FMRB_MEM_POOL_SIZE_HAL_FILE (1*1024)


//PSRAM
#define FMRB_MEM_POOL_SIZE_KERNEL (500*1024)
#define FMRB_MEM_POOL_SIZE_SYSTEM_APP (500*1024)
#define FMRB_MEM_POOL_SIZE_USER_APP (500*1024)

#define FMRB_USER_APP_COUNT 3

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

// Pool management (thread-safe)
fmrb_mem_handle_t fmrb_malloc_create_handle(void* pool, size_t size);
int fmrb_malloc_destroy_handle(fmrb_mem_handle_t handle);

// Memory allocation functions
void* fmrb_malloc(fmrb_mem_handle_t handle, size_t size);
void* fmrb_calloc(fmrb_mem_handle_t handle, size_t nmemb, size_t size);
void* fmrb_realloc(fmrb_mem_handle_t handle, void* ptr, size_t size);
void fmrb_free(fmrb_mem_handle_t handle, void* ptr);

// Pool operations
int32_t fmrb_malloc_check(fmrb_mem_handle_t handle);
int fmrb_get_stats(fmrb_mem_handle_t handle, fmrb_pool_stats_t* stats);

#ifdef __cplusplus
}
#endif
