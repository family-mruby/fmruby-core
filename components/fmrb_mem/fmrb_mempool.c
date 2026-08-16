#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_heap_caps.h"
#endif
#include "fmrb_hal.h"
#include "fmrb_mem.h"
#include "fmrb_log.h"

#define TAG "MEMPOOL"

EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_system[FMRB_MEM_POOL_SIZE_SYSTEM];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_kernel[FMRB_MEM_POOL_SIZE_KERNEL];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_system_app[FMRB_MEM_POOL_SIZE_SYSTEM_APP];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_system_overlay[FMRB_MEM_POOL_SIZE_SYSTEM_OVERLAY];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_user_app0[FMRB_MEM_POOL_SIZE_USER_APP];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_user_app1[FMRB_MEM_POOL_SIZE_USER_APP];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_user_app2[FMRB_MEM_POOL_SIZE_USER_APP];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_user_app_large[FMRB_MEM_POOL_SIZE_USER_APP_LARGE];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_editor_doc[FMRB_MEM_POOL_SIZE_EDITOR_DOC];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_tmpfs[FMRB_MEM_POOL_SIZE_TMPFS];
EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_log_buffer[FMRB_MEM_POOL_SIZE_LOG_BUFFER];

// POOL_ID_USER_APP3/4 are absent on purpose: they are filled in by
// fmrb_mempool_reserve the first time an app is given that slot.
static unsigned char* g_mempool_list[POOL_ID_MAX] = {
    [POOL_ID_SYSTEM]     = g_mempool_system,
    [POOL_ID_KERNEL]     = g_mempool_kernel,
    [POOL_ID_SYSTEM_APP]     = g_mempool_system_app,
    [POOL_ID_SYSTEM_OVERLAY] = g_mempool_system_overlay,
    [POOL_ID_USER_APP0]      = g_mempool_user_app0,
    [POOL_ID_USER_APP1]  = g_mempool_user_app1,
    [POOL_ID_USER_APP2]  = g_mempool_user_app2,
    [POOL_ID_USER_APP_LARGE] = g_mempool_user_app_large,
    [POOL_ID_EDITOR_DOC] = g_mempool_editor_doc,
    [POOL_ID_TMPFS]      = g_mempool_tmpfs,
    [POOL_ID_LOG_BUFFER] = g_mempool_log_buffer,
};

void* fmrb_get_mempool_ptr(int32_t id){
    if(id < 0 || id >= POOL_ID_MAX)
    {
        return NULL;
    }
    return g_mempool_list[id];
}

// Make sure a pool has memory behind it, allocating it from PSRAM if this is a
// slot that is not reserved statically. Returns false when PSRAM cannot spare
// it, which is how a machine with less memory ends up allowing fewer apps than
// the slot count would suggest.
//
// Once taken, the block is kept: an app slot is reused as apps come and go, and
// handing 500 KB back to the PSRAM heap only to ask for it again is how that
// heap gets fragmented over a long session.
bool fmrb_mempool_reserve(int32_t id)
{
    if (id < 0 || id >= POOL_ID_MAX) {
        return false;
    }
    if (g_mempool_list[id]) {
        return true;
    }

    size_t size = fmrb_get_mempool_size(id);
    if (size == 0) {
        return false;
    }

#ifdef CONFIG_IDF_TARGET_LINUX
    unsigned char *p = (unsigned char *)malloc(size);
#else
    unsigned char *p = (unsigned char *)heap_caps_aligned_alloc(8, size, MALLOC_CAP_SPIRAM);
#endif
    if (!p) {
        FMRB_LOGE(TAG, "Pool %d: no PSRAM for %zu bytes", (int)id, size);
        return false;
    }
    g_mempool_list[id] = p;
    FMRB_LOGI(TAG, "Pool %d reserved from PSRAM: %zu KB at %p", (int)id, size / 1024, p);
    return true;
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
        case POOL_ID_SYSTEM_OVERLAY:
            return FMRB_MEM_POOL_SIZE_SYSTEM_OVERLAY;
        case POOL_ID_USER_APP0:
        case POOL_ID_USER_APP1:
        case POOL_ID_USER_APP2:
        case POOL_ID_USER_APP3:
        case POOL_ID_USER_APP4:
            return FMRB_MEM_POOL_SIZE_USER_APP;
        case POOL_ID_USER_APP_LARGE:
            return FMRB_MEM_POOL_SIZE_USER_APP_LARGE;
        case POOL_ID_EDITOR_DOC:
            return FMRB_MEM_POOL_SIZE_EDITOR_DOC;
        case POOL_ID_TMPFS:
            return FMRB_MEM_POOL_SIZE_TMPFS;
        case POOL_ID_LOG_BUFFER:
            return FMRB_MEM_POOL_SIZE_LOG_BUFFER;
        default:
            return 0;
    }
}

static const char* fmrb_get_mempool_name(int32_t id){
    switch(id){
        case POOL_ID_SYSTEM:     return "SYSTEM";
        case POOL_ID_KERNEL:     return "KERNEL";
        case POOL_ID_SYSTEM_APP:     return "SYSTEM_APP";
        case POOL_ID_SYSTEM_OVERLAY: return "SYS_OVERLAY";
        case POOL_ID_USER_APP0:      return "USER_APP0";
        case POOL_ID_USER_APP1:  return "USER_APP1";
        case POOL_ID_USER_APP2:  return "USER_APP2";
        case POOL_ID_USER_APP_LARGE: return "USER_LARGE";
        case POOL_ID_EDITOR_DOC: return "EDITOR_DOC";
        case POOL_ID_TMPFS:      return "TMPFS";
        case POOL_ID_LOG_BUFFER: return "LOG_BUFFER";
        default:                 return "UNKNOWN";
    }
}

void fmrb_mempool_print_ranges(void){
    FMRB_LOGI(TAG, "Memory Pool Address Ranges:");
    for(int32_t id = 0; id < POOL_ID_MAX; id++){
        void* pool = g_mempool_list[id];
        size_t size = fmrb_get_mempool_size(id);
        FMRB_LOGI(TAG, "  %-12s %p - %p (%zu bytes)",
                      fmrb_get_mempool_name(id),
                      pool,
                      (unsigned char*)pool + size,
                      size);
    }
}

void fmrb_mempool_check_pointer(const void* ptr){
    if(ptr == NULL){
        FMRB_LOGI(TAG, "Pointer check: NULL");
        return;
    }

    // Check memory pools
    for(int32_t id = 0; id < POOL_ID_MAX; id++){
        void* pool = g_mempool_list[id];
        size_t size = fmrb_get_mempool_size(id);
        if(ptr >= pool && ptr < (void*)((unsigned char*)pool + size)){
            FMRB_LOGI(TAG, "Pointer %p is in %s pool [%p - %p]",
                          ptr,
                          fmrb_get_mempool_name(id),
                          pool,
                          (unsigned char*)pool + size);
            return;
        }
    }

    FMRB_LOGI(TAG, "Pointer %p is NOT in any memory pool (external memory or invalid)", ptr);
}
