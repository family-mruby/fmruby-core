#pragma once
    
enum FMRB_MEM_POOL_ID{
    POOL_ID_SYSTEM = 0,
    POOL_ID_KERNEL,
    POOL_ID_SYSTEM_APP,
    POOL_ID_SYSTEM_OVERLAY,
    POOL_ID_USER_APP0,
    POOL_ID_USER_APP1,
    POOL_ID_USER_APP2,
    POOL_ID_USER_APP_LARGE,
    // Editor document arena (picoruby-fmrb-editor-core). Deliberately its own
    // pool: the document must not sit in the app's 500KB mruby pool (it would
    // cap the file size and feed the GC's malloc accounting) nor in the shared
    // 500KB SYSTEM pool (that is the drivers' wallet, and a 1MB document does
    // not fit in it at all).
    POOL_ID_EDITOR_DOC,
    POOL_ID_LOG_BUFFER,
    POOL_ID_MAX
};

typedef int8_t fmrb_mem_handle_t;

//SRAM
#define FMRB_MEM_POOL_SIZE_HAL_FILE (1*1024)


//PSRAM
#define FMRB_MEM_POOL_SIZE_SYSTEM (500*1024) //for system functions
#define FMRB_MEM_POOL_SIZE_KERNEL (500*1024)
// 64-bit Linux doubles object headers/pointers vs 32-bit ESP32, so the same
// desktop has ~2x the live set on Linux; sizing both alike is inaccurate. The
// ESP32 800KB stays put pending 32-bit measurement + sp_gc_hdr slimming.
#ifdef CONFIG_IDF_TARGET_LINUX
#define FMRB_MEM_POOL_SIZE_SYSTEM_APP (1536*1024)  /* 64-bit object model ~2x + headroom */
#else
#define FMRB_MEM_POOL_SIZE_SYSTEM_APP (800*1024)
#endif
#define FMRB_MEM_POOL_SIZE_SYSTEM_OVERLAY (500*1024)
#define FMRB_MEM_POOL_SIZE_USER_APP (500*1024)
#define FMRB_MEM_POOL_SIZE_USER_APP_LARGE (1024*1024)
// Editor document arena. 1MB holds a ~700KB source file with the line index and
// the per-line highlight cache; PSRAM headroom is 3.2MB on S3 and >20MB on P4.
#define FMRB_MEM_POOL_SIZE_EDITOR_DOC (1024*1024)

#define FMRB_MEM_POOL_SIZE_LOG_BUFFER (128*1024)

#define FMRB_USER_APP_COUNT 3
