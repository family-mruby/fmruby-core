// Place the esp_lcd DPI panel's frame buffer on a page boundary.
//
// Shared by the two Modern display devices (lgfx_naryav4.hpp, lgfx_tab5.hpp).
//
// Why: the DSI scanout DMA reads the frame buffer from PSRAM all the time, and
// a buffer that is only cache-line aligned makes its bursts cross 4KB
// boundaries (which an AXI burst may not), so they get split and the scanout
// starves into the "underrun" that paints the rest of a frame blue. Measured
// on NARYA v4 (1280x720 RGB888 80MHz): 16-18 underruns in the boot window and
// 6-7 per minute of app launches at 0x...4dc0, none at all once the buffer
// sits on 4KB (doc/naryav4/report/p6.md).
//
// How: esp_lcd allocates the buffer itself (heap_caps_calloc, cache-line
// alignment only) and has no way to take a buffer or an alignment from the
// caller, so the heap is shaped right before it allocates:
//
//   1. probe where an allocation of that size and those caps lands now,
//   2. if it is off the boundary, hold a PSRAM pad at that spot, grown by
//      exactly the missing bytes, so the next allocation starts past it,
//   3. repeat until the probe lands aligned.
//
// The pad is taken as a big block first and shrunk in place, which pins it to
// the start of the free region the probe used (a small malloc could land in
// any hole). It is never freed -- the panel lives as long as the firmware --
// and costs a few KB of PSRAM and no internal RAM.
//
// Only the FIRST frame buffer the driver allocates after this call is placed.
// Nothing else may take PSRAM between this call and that allocation (the DSI
// driver's own objects are internal: CONFIG_LCD_DSI_OBJ_FORCE_INTERNAL).
//
// If anything does not go as expected the pad is dropped and the buffer lands
// where it always did: the panel still works, only unaligned (callers log
// which). Returns the pad size in bytes.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_heap_caps.h"

#include "fmrb_log.h"

// Boundary the DPI frame buffer is placed on.
#define DPI_FB_ALIGN            4096

static inline bool dpi_fb_is_aligned(const void *fb)
{
    return ((uintptr_t)fb & (DPI_FB_ALIGN - 1)) == 0;
}

// fb_size: bytes of ONE frame buffer, exactly as esp_lcd computes it
// (h_size * v_size * bits_per_pixel / 8). tag: log tag for the warning.
static inline size_t dpi_fb_align_next(size_t fb_size, const char *tag)
{
    const uint32_t fb_caps  = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_DMA;
    const uint32_t pad_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    void  *pad_block = nullptr;
    size_t pad_size  = 0;

    for (int attempt = 0; attempt < 8; ++attempt) {
        void *probe = heap_caps_malloc(fb_size, fb_caps);
        if (!probe) break;
        const uintptr_t at = (uintptr_t)probe;
        heap_caps_free(probe);

        const size_t off = at & (DPI_FB_ALIGN - 1);
        if (off == 0) return pad_size;
        const size_t grow = DPI_FB_ALIGN - off;

        if (!pad_block) {
            // Take the region the probe came from (only a block that big
            // fits there), then give back all but the missing bytes.
            void *big = heap_caps_malloc(fb_size + DPI_FB_ALIGN, pad_caps);
            if (!big) break;
            if ((uintptr_t)big > at || at - (uintptr_t)big > 256) {
                heap_caps_free(big);
                break;
            }
            void *pad = heap_caps_realloc(big, grow, pad_caps);
            if (pad != big) {
                heap_caps_free(pad ? pad : big);
                break;
            }
            pad_block = pad;
            pad_size = grow;
        } else {
            void *pad = heap_caps_realloc(pad_block, pad_size + grow, pad_caps);
            if (pad != pad_block) {
                heap_caps_free(pad ? pad : pad_block);
                pad_block = nullptr;
                pad_size = 0;
                break;
            }
            pad_size += grow;
        }
    }
    FMRB_LOGW(tag, "could not place the frame buffer on a %u-byte boundary",
              (unsigned)DPI_FB_ALIGN);
    if (pad_block) heap_caps_free(pad_block);
    return 0;
}
