#pragma once

enum FMRB_MEM_POOL_ID{
    POOL_ID_KERNEL = 0,
    POOL_ID_SYSTEM_APP,
    POOL_ID_USER_APP0,
    POOL_ID_USER_APP1,
    POOL_ID_USER_APP2,
    POOL_ID_MAX
};

#define FMRB_MEM_POOL_SIZE_KERNEL (500*1024)
#define FMRB_MEM_POOL_SIZE_SYSTEM_APP (500*1024)
#define FMRB_MEM_POOL_SIZE_USER_APP (500*1024)

#define FMRB_USER_APP_COUNT 3

void* fmrb_get_mempool_ptr(int id);
void* fmrb_get_mempool_app_ptr(int no);
size_t fmrb_get_mempool_size(int id);
