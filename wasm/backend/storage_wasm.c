/*
 * storage_wasm.c - the browser's storage room, as the page last saw it.
 *
 * FmrbApp.free_bytes("/home...") has to answer at once, from the machine's
 * own thread, but the browser tells its storage only through a promise on
 * the page (navigator.storage.estimate(), quota less usage). So the page
 * asks at the start and every few seconds, and writes what it saw, in KiB,
 * to this cell in the shared memory (main.js, shareStorageRoom); the
 * machine reads the cell. Emscripten's statvfs is a constant, so there is
 * nothing better to ask on this side.
 *
 * The page writes a tenth less than the estimate (it is an estimate: the
 * browser counts in its own way, and /home also lives in the module's
 * memory until it is synced).
 */

#include <emscripten.h>
#include <stdint.h>

#define STORAGE_UNKNOWN 0xFFFFFFFFu

static volatile uint32_t s_free_kib = STORAGE_UNKNOWN;

EMSCRIPTEN_KEEPALIVE volatile uint32_t *fmrb_wasm_storage_free_kib_ptr(void) { return &s_free_kib; }

/* Bytes the page last saw free for /home, or -1 before it has said. */
int64_t fmrb_wasm_storage_free(void)
{
    uint32_t kib = __atomic_load_n(&s_free_kib, __ATOMIC_RELAXED);
    return kib == STORAGE_UNKNOWN ? -1 : (int64_t)kib * 1024;
}
