#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#ifndef CONFIG_IDF_TARGET_LINUX
#include "esp_heap_caps.h"
#endif
#include "fmrb_hal.h"
#include "fmrb_mem.h"
#include "fmrb_log.h"
#include "fmrb_attr.h"

#define TAG "MEMPOOL"

FMRB_EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_system[FMRB_MEM_POOL_SIZE_SYSTEM];
FMRB_EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_kernel[FMRB_MEM_POOL_SIZE_KERNEL];
FMRB_EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_system_app[FMRB_MEM_POOL_SIZE_SYSTEM_APP];
FMRB_EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_system_overlay[FMRB_MEM_POOL_SIZE_SYSTEM_OVERLAY];
// Each row of these is one pool; the sizes are multiples of 8, so every row
// keeps the alignment of the first.
FMRB_EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_user_app[FMRB_USER_APP_STATIC_POOL_COUNT][FMRB_MEM_POOL_SIZE_USER_APP];
FMRB_EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_user_app_large[FMRB_USER_APP_LARGE_POOL_COUNT][FMRB_MEM_POOL_SIZE_USER_APP_LARGE];
FMRB_EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_editor_doc[FMRB_MEM_POOL_SIZE_EDITOR_DOC];
FMRB_EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_tmpfs[FMRB_MEM_POOL_SIZE_TMPFS];
FMRB_EXT_RAM_BSS_ATTR static unsigned char __attribute__((aligned(8))) g_mempool_log_buffer[FMRB_MEM_POOL_SIZE_LOG_BUFFER];

// The ids past FMRB_USER_APP_POOL_COUNT and past the large pool count are not
// this build's pools at all: their size is 0 and they have no memory, so
// fmrb_mempool_reserve refuses them.
_Static_assert(FMRB_USER_APP_POOL_COUNT >= FMRB_USER_APP_STATIC_POOL_COUNT,
               "there must be at least as many app pools as statically reserved ones");
_Static_assert(FMRB_USER_APP_POOL_COUNT <= FMRB_USER_APP_COUNT,
               "the ordinary pools must fit in their id run");
_Static_assert(FMRB_USER_APP_LARGE_POOL_COUNT >= 1 &&
               POOL_ID_USER_APP_LARGE + FMRB_USER_APP_LARGE_POOL_COUNT <= POOL_ID_USER_APP_LARGE_END,
               "the large pools must fit in their id run");
_Static_assert(POOL_ID_USER_APP4 < POOL_ID_USER_APP_LARGE,
               "the named user app pools must fit inside the user app run");

// The ordinary pools past FMRB_USER_APP_STATIC_POOL_COUNT: empty until
// fmrb_mempool_reserve fills them in, the first time an app is given one. The
// static pools are not in a table: fmrb_get_mempool_ptr names them in code, so
// a table of addresses (one word per pool id, in internal RAM) does not have to
// grow with the pool ids. The S3 has no on-demand pools and no table at all.
#define DYN_POOL_COUNT (FMRB_USER_APP_POOL_COUNT - FMRB_USER_APP_STATIC_POOL_COUNT)
#if DYN_POOL_COUNT > 0
static unsigned char* g_mempool_dyn[DYN_POOL_COUNT];
#endif

void* fmrb_get_mempool_ptr(int32_t id){
    if (id >= POOL_ID_USER_APP0 && id < POOL_ID_USER_APP0 + FMRB_USER_APP_POOL_COUNT) {
        const int32_t n = id - POOL_ID_USER_APP0;
        if (n < FMRB_USER_APP_STATIC_POOL_COUNT) {
            return g_mempool_user_app[n];
        }
#if DYN_POOL_COUNT > 0
        return g_mempool_dyn[n - FMRB_USER_APP_STATIC_POOL_COUNT];
#else
        return NULL;
#endif
    }
    if (id >= POOL_ID_USER_APP_LARGE && id < POOL_ID_USER_APP_LARGE + FMRB_USER_APP_LARGE_POOL_COUNT) {
        return g_mempool_user_app_large[id - POOL_ID_USER_APP_LARGE];
    }
    switch (id) {
        case POOL_ID_SYSTEM:         return g_mempool_system;
        case POOL_ID_KERNEL:         return g_mempool_kernel;
        case POOL_ID_SYSTEM_APP:     return g_mempool_system_app;
        case POOL_ID_SYSTEM_OVERLAY: return g_mempool_system_overlay;
        case POOL_ID_EDITOR_DOC:     return g_mempool_editor_doc;
        case POOL_ID_TMPFS:          return g_mempool_tmpfs;
        case POOL_ID_LOG_BUFFER:     return g_mempool_log_buffer;
        default:                     return NULL;
    }
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
    if (fmrb_get_mempool_ptr(id)) {
        return true;
    }

    size_t size = fmrb_get_mempool_size(id);
    if (size == 0) {
        return false;
    }
#if DYN_POOL_COUNT > 0
    // Only an on-demand ordinary pool gets here: every other pool with a size
    // is static and was answered above.
    unsigned char **slot = &g_mempool_dyn[id - POOL_ID_USER_APP0 - FMRB_USER_APP_STATIC_POOL_COUNT];

#ifdef CONFIG_IDF_TARGET_LINUX
    unsigned char *p = (unsigned char *)malloc(size);
#else
    unsigned char *p = (unsigned char *)heap_caps_aligned_alloc(8, size, MALLOC_CAP_SPIRAM);
#endif
    if (!p) {
        FMRB_LOGE(TAG, "Pool %d: no PSRAM for %zu bytes", (int)id, size);
        return false;
    }
    *slot = p;
    FMRB_LOGI(TAG, "Pool %d reserved from PSRAM: %zu KB at %p", (int)id, size / 1024, p);
    return true;
#else
    return false;
#endif
}

void* fmrb_get_mempool_app_ptr(int32_t no){
    if(no < 0 || no >= FMRB_USER_APP_COUNT)
    {
        return NULL;
    }
    return fmrb_get_mempool_ptr(no + POOL_ID_USER_APP0);
}

size_t fmrb_get_mempool_size(int32_t id){
    // Two runs of identically sized pools, the ordinary and the large ones;
    // only their ends have names, so they are matched as ranges rather than
    // case by case. An id in a run past this build's count has no size.
    if (id >= POOL_ID_USER_APP0 && id < POOL_ID_USER_APP0 + FMRB_USER_APP_POOL_COUNT) {
        return FMRB_MEM_POOL_SIZE_USER_APP;
    }
    if (id >= POOL_ID_USER_APP_LARGE && id < POOL_ID_USER_APP_LARGE + FMRB_USER_APP_LARGE_POOL_COUNT) {
        return FMRB_MEM_POOL_SIZE_USER_APP_LARGE;
    }
    switch(id){
        case POOL_ID_SYSTEM:
            return FMRB_MEM_POOL_SIZE_SYSTEM;
        case POOL_ID_KERNEL:
            return FMRB_MEM_POOL_SIZE_KERNEL;
        case POOL_ID_SYSTEM_APP:
            return FMRB_MEM_POOL_SIZE_SYSTEM_APP;
        case POOL_ID_SYSTEM_OVERLAY:
            return FMRB_MEM_POOL_SIZE_SYSTEM_OVERLAY;
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

bool fmrb_mempool_is_user_large(int32_t id){
    return id >= POOL_ID_USER_APP_LARGE &&
           id < POOL_ID_USER_APP_LARGE + FMRB_USER_APP_LARGE_POOL_COUNT;
}

static const char* fmrb_get_mempool_name(int32_t id){
    // Same run as above; the callers print the id next to the name, so the
    // slots do not each need one of their own.
    if (id >= POOL_ID_USER_APP0 && id < POOL_ID_USER_APP_LARGE) {
        return "USER_APP";
    }
    if (id >= POOL_ID_USER_APP_LARGE && id < POOL_ID_USER_APP_LARGE_END) {
        return "USER_LARGE";
    }
    switch(id){
        case POOL_ID_SYSTEM:     return "SYSTEM";
        case POOL_ID_KERNEL:     return "KERNEL";
        case POOL_ID_SYSTEM_APP:     return "SYSTEM_APP";
        case POOL_ID_SYSTEM_OVERLAY: return "SYS_OVERLAY";
        case POOL_ID_EDITOR_DOC: return "EDITOR_DOC";
        case POOL_ID_TMPFS:      return "TMPFS";
        case POOL_ID_LOG_BUFFER: return "LOG_BUFFER";
        default:                 return "UNKNOWN";
    }
}

void fmrb_mempool_print_ranges(void){
    FMRB_LOGI(TAG, "Memory Pool Address Ranges:");
    for(int32_t id = 0; id < POOL_ID_MAX; id++){
        void* pool = fmrb_get_mempool_ptr(id);
        size_t size = fmrb_get_mempool_size(id);
        FMRB_LOGI(TAG, "  %2d %-12s %p - %p (%zu bytes)",
                      (int)id,
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
        void* pool = fmrb_get_mempool_ptr(id);
        size_t size = fmrb_get_mempool_size(id);
        if(ptr >= pool && ptr < (void*)((unsigned char*)pool + size)){
            FMRB_LOGI(TAG, "Pointer %p is in %s pool #%d [%p - %p]",
                          ptr,
                          fmrb_get_mempool_name(id),
                          (int)id,
                          pool,
                          (unsigned char*)pool + size);
            return;
        }
    }

    FMRB_LOGI(TAG, "Pointer %p is NOT in any memory pool (external memory or invalid)", ptr);
}
