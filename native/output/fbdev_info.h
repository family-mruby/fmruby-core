/*
 * fbdev_info.h - the processing half of the fbdev output: what /proc/fb and
 * the FBIOGET_VSCREENINFO / FBIOGET_FSCREENINFO answers mean, as plain
 * functions over the kernel's structs (output_fbdev.c makes the calls; the
 * host tests hand in made-up answers, test/native/).
 */
#pragma once

#include <linux/fb.h>
#include <stdbool.h>
#include <stddef.h>

#include "fmrb_present.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The number of the framebuffer named `name` in /proc/fb's text
 * ("0 fb_st7789v\n1 vc4drmfb\n"); name "" takes the first. -1 when absent. */
int native_fbdev_find(const char *proc_fb, const char *name);

/* The surface a mapped framebuffer is (pixels left NULL, set by the caller
 * at map + offset): RGB565 or XRGB8888 by the bit fields, the visible area at
 * xoffset/yoffset. False, with the reason, for formats the present core does
 * not write (BGR orders, 24 bit, palettes). *offset is the byte offset of the
 * visible area in the mapping. */
bool native_fbdev_surface(const struct fb_var_screeninfo *var,
                          const struct fb_fix_screeninfo *fix, int rotate,
                          fmrb_present_surface_t *surface, size_t *offset,
                          char *why, size_t why_len);

#ifdef __cplusplus
}
#endif
