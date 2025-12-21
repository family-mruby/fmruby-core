#pragma once
    
enum FMRB_MEM_POOL_ID{
    POOL_ID_SYSTEM = 0,
    POOL_ID_KERNEL,
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
#define FMRB_MEM_POOL_SIZE_SYSTEM (500*1024) //for system functions
#define FMRB_MEM_POOL_SIZE_KERNEL (500*1024)
#define FMRB_MEM_POOL_SIZE_SYSTEM_APP (500*1024)
#define FMRB_MEM_POOL_SIZE_USER_APP (500*1024)

#define FMRB_USER_APP_COUNT 3
