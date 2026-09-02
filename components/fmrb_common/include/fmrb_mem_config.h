#pragma once

// One pool per user app slot, so the slot count is the app ceiling.
#include "fmrb_limits.h"

#define FMRB_USER_APP_COUNT FMRB_MAX_USER_APPS

enum FMRB_MEM_POOL_ID{
    POOL_ID_SYSTEM = 0,
    POOL_ID_KERNEL,
    POOL_ID_SYSTEM_APP,
    POOL_ID_SYSTEM_OVERLAY,
    // One pool per user app slot. The first FMRB_USER_APP_STATIC_POOL_COUNT are
    // reserved statically; the rest are taken from PSRAM the first time an app
    // lands in them (see fmrb_mempool_reserve). Reserving all of them
    // statically would put 7.3 MB of the Retro machine's 8 MB PSRAM into .bss
    // whether or not anyone ever runs five apps, which is most of what is left
    // after the editor document and /tmp arenas.
    POOL_ID_USER_APP0,
    POOL_ID_USER_APP1,
    POOL_ID_USER_APP2,
    POOL_ID_USER_APP3,
    POOL_ID_USER_APP4,
    // A build that raises FMRB_MAX_APPS (the web one does) gets the remaining
    // slots as unnamed ids in the same run; nothing refers to them singly, so
    // only the end of the run has a name. fmrb_mempool.c asserts that the five
    // named ones still fit inside it.
    POOL_ID_USER_APP_LARGE = POOL_ID_USER_APP0 + FMRB_USER_APP_COUNT,
    // Editor document arena (picoruby-fmrb-editor-core). Deliberately its own
    // pool: the document must not sit in the app's 500KB mruby pool (it would
    // cap the file size and feed the GC's malloc accounting) nor in the shared
    // 500KB SYSTEM pool (that is the drivers' wallet, and a 1MB document does
    // not fit in it at all).
    POOL_ID_EDITOR_DOC,
    // /tmp RAM filesystem arena (doc/multivm_app/plan.md 3.1). Its own pool for
    // the same reasons as the editor document: a file written by one app must
    // not be charged to that app's mruby pool (it would die with the app and
    // cap the file size), and the shared SYSTEM pool is the drivers' wallet.
    POOL_ID_TMPFS,
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
#elif defined(FMRB_HW_FAMILY_MODERN)
// 800KB was an S3 number and the desktop has outgrown it the same way the
// service host outgrew its 500KB below. Measured on a NARYA v4: the desktop
// sits at 549KB of 800KB before anything happens (the compiled irep of a
// 213KB script is most of it), and a launcher rescan takes it to 632KB --
// 77%, where it stays. At that occupancy the collector thrashes: a full
// GC.start costs 0.27 s when the heap has room, but the burst at the end of
// a rescan cost 8.8 SECONDS of collection, during which the desktop sends
// nothing and looks dead. Twice it went over the edge and the task ended
// with no exception at all -- the same silent death the tts service had.
// Modern has 32MB of PSRAM with 12MB free; this is the cheapest fix there is.
#define FMRB_MEM_POOL_SIZE_SYSTEM_APP (1536*1024)
#else
#define FMRB_MEM_POOL_SIZE_SYSTEM_APP (800*1024)
#endif
#define FMRB_MEM_POOL_SIZE_SYSTEM_OVERLAY (500*1024)
// Same 64-bit rule as SYSTEM_APP above: the identical script costs about
// twice the pool on Linux (doubled headers and pointers, and the compile
// peak doubles with them -- a 23 KB source that fits the device's 500 KB
// aborted the sim). 2x plus headroom; host RAM is not scarce.
#ifdef CONFIG_IDF_TARGET_LINUX
#define FMRB_MEM_POOL_SIZE_USER_APP (1536*1024)
#define FMRB_MEM_POOL_SIZE_USER_APP_LARGE (3072*1024)
#elif defined(FMRB_HW_FAMILY_MODERN)
// Modern has 32MB of PSRAM and was using two of them. 500KB was an S3 number,
// and the service host has outgrown it: every resident service shares one VM,
// and compiling one more of them (the tts service) ran the pool dry at 90%
// full -- the host died during require with no exception to show for it.
#define FMRB_MEM_POOL_SIZE_USER_APP (1024*1024)
#define FMRB_MEM_POOL_SIZE_USER_APP_LARGE (2048*1024)
#else
#define FMRB_MEM_POOL_SIZE_USER_APP (500*1024)
#define FMRB_MEM_POOL_SIZE_USER_APP_LARGE (1024*1024)
#endif
// Editor document arena. 1MB holds a ~700KB source file with the line index and
// the per-line highlight cache; PSRAM headroom is 3.2MB on S3 and >20MB on P4.
#define FMRB_MEM_POOL_SIZE_EDITOR_DOC (1024*1024)

// /tmp RAM filesystem. 512KB is what the S3 can spare: PSRAM headroom there is
// 3.2MB and the editor document arena already claims 1MB of it.
//
// Modern gets 8MB. The workload that asked for it is spoken audio: the tts
// service caches what it has said, and a couple of seconds of speech is
// 200KB. That belongs in RAM rather than on the flash -- a machine that talks
// would otherwise rewrite its filesystem all day, and the internal flash is
// both small and finite. Of the P4's 32MB this is still a small share, and it
// holds about forty phrases.
#ifdef FMRB_HW_FAMILY_MODERN
#define FMRB_MEM_POOL_SIZE_TMPFS (8192*1024)
#define FMRB_TMPFS_CAPACITY_BYTES (8064*1024)
#else
#define FMRB_MEM_POOL_SIZE_TMPFS (512*1024)
#define FMRB_TMPFS_CAPACITY_BYTES (448*1024)
#endif
// The capacity is the bytes of file content /tmp will hand out. The gap to the
// pool size covers allocator headers and the per-file growth slack, so a write
// is refused with ENOSPC while the arena still has room -- failing mid-write is
// much harder for an app to survive than being told "full" before anything
// moved.

#define FMRB_MEM_POOL_SIZE_LOG_BUFFER (128*1024)

// How many of those pools exist before anyone asks. The rest are allocated on
// first use and then kept (see fmrb_mempool_reserve): allocating and freeing
// 500 KB blocks over a long session is how a PSRAM heap gets fragmented.
#define FMRB_USER_APP_STATIC_POOL_COUNT 3
