#pragma once
    
enum FMRB_MEM_POOL_ID{
    POOL_ID_SYSTEM = 0,
    POOL_ID_KERNEL,
    POOL_ID_SYSTEM_APP,
    POOL_ID_SYSTEM_OVERLAY,
    POOL_ID_USER_APP0,
    POOL_ID_USER_APP1,
    POOL_ID_USER_APP2,
    POOL_ID_LOG_BUFFER,
    POOL_ID_MAX
};

typedef int8_t fmrb_mem_handle_t;

#ifndef PRISM_POOL_SIZE
  #ifdef PRISM_BUILD_HOST
    // Host build (picorbc): needs ~220KB peak for compiling mrblib
    #define FMRB_MEM_PRISM_POOL_SIZE (288 * 1024)  // 288KB with safety margin
  #else
    // Target build: prism parser needs substantial memory for AST and constant pool.
    // 64KB was insufficient for scripts >4KB, causing silent constant pool allocation
    // failures (PM_CONSTANT_ID_UNSET) which led to SIGSEGV in mrc_resolve_intern.
    #define FMRB_MEM_PRISM_POOL_SIZE (192 * 1024)  // 192KB for user script compilation
  #endif
#endif

//SRAM
#define FMRB_MEM_POOL_SIZE_HAL_FILE (1*1024)


//PSRAM
#define FMRB_MEM_POOL_SIZE_SYSTEM (500*1024) //for system functions
#define FMRB_MEM_POOL_SIZE_KERNEL (500*1024)
#define FMRB_MEM_POOL_SIZE_SYSTEM_APP (800*1024)
#define FMRB_MEM_POOL_SIZE_SYSTEM_OVERLAY (500*1024)
#define FMRB_MEM_POOL_SIZE_USER_APP (500*1024)

#define FMRB_MEM_POOL_SIZE_LOG_BUFFER (128*1024)

#define FMRB_USER_APP_COUNT 3
