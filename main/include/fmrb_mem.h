#pragma once

enum FMRB_MEM_POOL_ID{
    POOL_ID_KERNEL = 0,
    POOL_ID_SYSTEM_APP,
    POOL_ID_USER_APP0,
    POOL_ID_USER_APP1,
    POOL_ID_USER_APP2,
    POOL_ID_MAX
};

#ifndef PRISM_POOL_SIZE
  #ifdef PRISM_BUILD_HOST
    // Host build (picorbc): needs ~220KB peak for compiling mrblib
    #define FMRB_MEM_PRISM_POOL_SIZE (288 * 1024)  // 288KB with safety margin
  #else
    // Target build (ESP32/Linux eval): much smaller user code
    #define FMRB_MEM_PRISM_POOL_SIZE (64 * 1024)   // 64KB initial - will measure actual usage
  #endif
#endif

#define FMRB_MEM_POOL_SIZE_KERNEL (500*1024)
#define FMRB_MEM_POOL_SIZE_SYSTEM_APP (500*1024)
#define FMRB_MEM_POOL_SIZE_USER_APP (500*1024)

#define FMRB_USER_APP_COUNT 3

void* fmrb_get_mempool_ptr(int id);
size_t fmrb_get_mempool_size(int id);
void* fmrb_get_mempool_app_ptr(int no);
