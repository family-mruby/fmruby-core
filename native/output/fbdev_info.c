/*
 * fbdev_info.c - see fbdev_info.h.
 */

#include "fbdev_info.h"

#include <stdio.h>
#include <string.h>

int native_fbdev_find(const char *proc_fb, const char *name)
{
    const char *p = proc_fb;
    while (p != NULL && *p != '\0') {
        int num = -1;
        char id[64] = { 0 };
        if (sscanf(p, "%d %63s", &num, id) == 2 && num >= 0) {
            if (name == NULL || name[0] == '\0' || strcmp(id, name) == 0) return num;
        }
        p = strchr(p, '\n');
        if (p != NULL) p++;
    }
    return -1;
}

static bool is_field(const struct fb_bitfield *f, unsigned offset, unsigned length)
{
    return f->offset == offset && f->length == length && f->msb_right == 0;
}

bool native_fbdev_surface(const struct fb_var_screeninfo *var,
                          const struct fb_fix_screeninfo *fix, int rotate,
                          fmrb_present_surface_t *surface, size_t *offset,
                          char *why, size_t why_len)
{
    memset(surface, 0, sizeof(*surface));
    if (fix->type != FB_TYPE_PACKED_PIXELS ||
        (fix->visual != FB_VISUAL_TRUECOLOR && fix->visual != FB_VISUAL_DIRECTCOLOR)) {
        snprintf(why, why_len, "framebuffer type %u visual %u is not packed true colour",
                 fix->type, fix->visual);
        return false;
    }
    if (var->bits_per_pixel == 16 && is_field(&var->red, 11, 5) &&
        is_field(&var->green, 5, 6) && is_field(&var->blue, 0, 5)) {
        surface->format = FMRB_PRESENT_RGB565;
    } else if (var->bits_per_pixel == 32 && is_field(&var->red, 16, 8) &&
               is_field(&var->green, 8, 8) && is_field(&var->blue, 0, 8)) {
        surface->format = FMRB_PRESENT_XRGB8888;
    } else {
        snprintf(why, why_len, "framebuffer format %u bpp, red %u/%u green %u/%u blue %u/%u "
                               "is not RGB565 or XRGB8888",
                 var->bits_per_pixel, var->red.offset, var->red.length, var->green.offset,
                 var->green.length, var->blue.offset, var->blue.length);
        return false;
    }
    int bpp = fmrb_present_bytes_per_pixel(surface->format);
    if (var->xres == 0 || var->yres == 0 || fix->line_length < var->xres * (unsigned)bpp) {
        snprintf(why, why_len, "framebuffer geometry %ux%u with %u bytes per row does not add up",
                 var->xres, var->yres, fix->line_length);
        return false;
    }
    size_t off = (size_t)var->yoffset * fix->line_length + (size_t)var->xoffset * (size_t)bpp;
    size_t need = off + (size_t)(var->yres - 1) * fix->line_length + (size_t)var->xres * (size_t)bpp;
    if (fix->smem_len != 0 && need > fix->smem_len) {
        snprintf(why, why_len, "framebuffer memory %u bytes is smaller than the visible area (%zu)",
                 fix->smem_len, need);
        return false;
    }
    if (!fmrb_present_rotate_valid(rotate)) {
        snprintf(why, why_len, "rotate %d is not 0, 90, 180 or 270", rotate);
        return false;
    }
    surface->w = (int)var->xres;
    surface->h = (int)var->yres;
    surface->stride_bytes = (int)fix->line_length;
    surface->rotate = rotate;
    *offset = off;
    return true;
}
