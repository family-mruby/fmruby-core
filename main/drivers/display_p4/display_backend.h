/*
 * display_backend.h - output side of the Modern (ESP32-P4) display driver
 *
 * display_p4_task.cpp keeps everything that decides WHAT the screen should
 * look like: the link receive loop, the gfx command interpretation, the canvas
 * and sprite stores, z-order, the viewport torus split, the sprite clip, the
 * remote-desktop capture, and the cursor. What is behind this interface is only
 * HOW the finished picture gets composited and put on the panel -- the part that
 * is hardware, and therefore the part the wasm target has to replace
 * (doc/wasm/, P3).
 *
 * Two implementations:
 *
 *   display_backend_ppa.cpp   PPA Blend for compositing, PPA SRM for the 3x
 *                             scale and 90-degree rotate into the DSI frame
 *                             buffer. The device default, moved here unchanged.
 *   display_backend_cpu.cpp   the same in software, on top of
 *                             display_blend_cpu.c, which is plain C with no
 *                             ESP-IDF in it so wasm can use it as is.
 *
 * Deliberately NOT in the interface:
 *
 *   - Allocating pixel memory. ppa_alloc_buffer() in display_p4_task.cpp
 *     allocates cache-line-aligned PSRAM, which is what canvases want too and
 *     is about the cache rather than about the PPA. Both backends use it.
 *   - Deciding what the cursor looks like. The bake/restore pair only touches
 *     the framebuffer sprite, and cursor_patch() composites the cursor over a
 *     framebuffer region itself; only putting that region on the panel without
 *     a full present is backend work (present_patch).
 *   - The panel itself. Both P4 backends drive the same LGFX_Tab5; it is Tab5
 *     hardware, not PPA. Reach it with display_p4_lcd().
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
    /* The LovyanGFX headers come in through M5GFX.h, the same way
     * lgfx_tab5.hpp takes them; the M5GFX device objects are not used. */
    #include <M5GFX.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One clipped source block on its way into the framebuffer.
 *
 * The caller has already clipped to framebuffer bounds and resolved the
 * viewport wrap, so w/h are what actually gets written and src/dst are where.
 * Everything here is RGB565 in PPA-native byte order.
 *
 * bg is both the background and the output: compositing is in place.
 *
 * The colour key marks a range rather than one value, because a canvas'
 * transparent colour is stored expanded to RGB888 and RGB565 quantisation
 * turns one 565 value into a band of 888 values. A foreground pixel whose
 * expanded r/g/b all fall inside the band is left alone and the background
 * shows through. Both backends must read the band the same way or transparent
 * edges differ; that equivalence is what the tiles baseline checks.
 */
/* Largest patch present_patch has to handle: the cursor, 16x16. A backend that
 * scales into a temporary sizes it from this, so the temporary stays the 4.6 KB
 * it has always been rather than growing with a guess. */
#define DISPLAY_PATCH_MAX_W 16
#define DISPLAY_PATCH_MAX_H 16

typedef struct {
    const void *fg;              /* source pixels */
    int         fg_pic_w;        /* full source width, for its row stride */
    int         fg_pic_h;        /* full source height, as the PPA is told it */
    int         src_x, src_y;    /* top-left of the block inside the source */
    size_t      fg_size;         /* allocated size of fg, bounds the cache flush */

    void       *bg;              /* framebuffer pixels, read and written */
    int         bg_pic_w;
    int         bg_pic_h;
    size_t      bg_size;         /* aligned allocation size, for the cache flush */
    int         dst_x, dst_y;

    int         w, h;            /* block size, already clipped */

    bool        color_key;
    uint8_t     ck_r_low, ck_g_low, ck_b_low;
    uint8_t     ck_r_high, ck_g_high, ck_b_high;

    /* Only for the PPA backend's error path and its log line. */
    uint16_t    canvas_id;
    bool        is_viewport;
} display_blend_req_t;

/* The software compositing core behind display_backend_cpu.cpp: plain C with
 * no ESP-IDF in it, shared with the wasm target. Defined in
 * display_blend_cpu.c. */
void display_blend_cpu_block(const display_blend_req_t *req);

#ifdef __cplusplus

typedef struct {
    const char *name;

    /* Register accelerators and take hold of the output surface. Called once
     * the panel is up and the framebuffer sprite exists. */
    void (*init)(int fb_w, int fb_h);

    /* Once, before the first composited frame reaches the panel: clear
     * whatever the boot screen left and make the surface safe to hand to DMA. */
    void (*first_frame)(void);

    /* Composite one block. Called several times per canvas for a viewport. */
    void (*blend_block)(const display_blend_req_t *req);

    /* Put the finished framebuffer on the panel: 3x and rotated to portrait.
     * fb_size is its aligned allocation size, which the cache flush needs. */
    void (*present)(LGFX_Sprite *fb, size_t fb_size);

    /* Put one small already-composited block on the panel, scaled the same way
     * present() scales, without redrawing anything else.
     *
     * This is the cursor fast path: a move repaints a 16x16 patch (~4.6 KB
     * scaled) instead of re-rendering and pushing the whole 1.8 MB screen. The
     * caller has composited the cursor over the framebuffer content already, so
     * the block is just pixels; (x0,y0) is where they came from in framebuffer
     * coordinates and where they go back. */
    void (*present_patch)(const uint16_t *block, int x0, int y0, int w, int h,
                          int fb_w, int fb_h);

    void (*shutdown)(void);
} display_backend_t;

/* The one the build selected. */
const display_backend_t *display_backend(void);

/* The panel, shared by both backends and by the parts of display_p4_task.cpp
 * that draw straight to it (the boot screen, the canvas sprites). */
LGFX_Device *display_p4_lcd(void);

/* The panel's own frame buffer, in its native portrait orientation, or NULL if
 * this panel does not expose one. Kept separate from display_p4_lcd() so this
 * header does not have to pull in the Tab5 panel definition. */
void *display_p4_panel_framebuffer(void);

/* Cache-line-aligned PSRAM, the allocator canvases and the framebuffer share.
 * Defined in display_p4_task.cpp. */
void *display_p4_alloc_pixels(size_t length, size_t *out_aligned_size);

#endif /* __cplusplus */

#ifdef __cplusplus
}
#endif
