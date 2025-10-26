#include <stddef.h>
#include <stdint.h>
#include "esp_attr.h"
#include "fmrb_mem.h"

EXT_RAM_BSS_ATTR unsigned char __attribute__((aligned(8))) g_prism_memory_pool[FMRB_MEM_PRISM_POOL_SIZE];

EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_system[FMRB_MEM_POOL_SIZE_SYSTEM];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_kernel[FMRB_MEM_POOL_SIZE_KERNEL];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_system_app[FMRB_MEM_POOL_SIZE_SYSTEM_APP];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_user_app0[FMRB_MEM_POOL_SIZE_USER_APP];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_user_app1[FMRB_MEM_POOL_SIZE_USER_APP];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_user_app2[FMRB_MEM_POOL_SIZE_USER_APP];

static unsigned char* g_mempool_list[POOL_ID_MAX] = {
    [POOL_ID_SYSTEM]     = g_mempool_system,
    [POOL_ID_KERNEL]     = g_mempool_kernel,
    [POOL_ID_SYSTEM_APP] = g_mempool_system_app,
    [POOL_ID_USER_APP0]  = g_mempool_user_app0,
    [POOL_ID_USER_APP1]  = g_mempool_user_app1,
    [POOL_ID_USER_APP2]  = g_mempool_user_app2,
};

void* fmrb_get_mempool_ptr(int32_t id){
    if(id < 0 || id >= POOL_ID_MAX)
    {
        return NULL;
    }
    return g_mempool_list[id];
}

void* fmrb_get_mempool_app_ptr(int32_t no){
    if(no < 0 || no >= FMRB_USER_APP_COUNT)
    {
        return NULL;
    }
    return g_mempool_list[no + POOL_ID_USER_APP0];
}

size_t fmrb_get_mempool_size(int32_t id){
    switch(id){
        case POOL_ID_SYSTEM:
            return FMRB_MEM_POOL_SIZE_SYSTEM;
        case POOL_ID_KERNEL:
            return FMRB_MEM_POOL_SIZE_KERNEL;
        case POOL_ID_SYSTEM_APP:
            return FMRB_MEM_POOL_SIZE_SYSTEM_APP;
        case POOL_ID_USER_APP0:
        case POOL_ID_USER_APP1:
        case POOL_ID_USER_APP2:
            return FMRB_MEM_POOL_SIZE_USER_APP;
        default:
            return 0;
    }
}
