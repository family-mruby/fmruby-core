/*
 * output_fbdev.c - a Linux framebuffer as an output (native_backends.h): the
 * Cardputer Zero's LCD (fb_st7789v, an fbtft panel), or any /dev/fbN.
 *
 * display.device picks it: "auto:<name>" looks the name up in /proc/fb (the
 * number moves with the drivers loaded, so the definition does not say fb0),
 * "auto" takes the first framebuffer, a path is opened as it is. The format,
 * the size and the row length come from the device (fbdev_info.c); the frame
 * is drawn straight into the mapping through the present core, at the
 * largest whole scale that fits (or display.scale), turned by
 * display.rotate. A panel driver with deferred I/O (fbtft) sends the pages
 * written to the panel by itself.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "fbdev_info.h"
#include "fmrb_present.h"
#include "native_backends.h"

static int s_fd = -1;
static uint8_t *s_map = NULL;
static size_t s_map_len = 0;
static fmrb_present_surface_t s_surface;
static int s_scale = 0;
static char s_path[NATIVE_MACHINE_STR];

static bool resolve_device(const char *device, char *path, size_t len, char *why, size_t why_len)
{
    if (strncmp(device, "auto", 4) != 0) {
        snprintf(path, len, "%s", device);
        return true;
    }
    const char *name = device[4] == ':' ? device + 5 : "";
    char text[1024] = { 0 };
    FILE *f = fopen("/proc/fb", "r");
    if (f == NULL) {
        snprintf(why, why_len, "no /proc/fb (%s): this system has no framebuffer", strerror(errno));
        return false;
    }
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    int num = native_fbdev_find(text, name);
    if (num < 0) {
        if (text[0] == '\0') snprintf(why, why_len, "no framebuffer: /proc/fb is empty");
        else snprintf(why, why_len, "no framebuffer named \"%s\" in /proc/fb (it lists: %.200s)", name, text);
        return false;
    }
    snprintf(path, len, "/dev/fb%d", num);
    return true;
}

static bool fbdev_open(const native_machine_t *m, char *why, size_t why_len)
{
    if (!resolve_device(m->display.device, s_path, sizeof(s_path), why, why_len)) return false;
    s_fd = open(s_path, O_RDWR | O_CLOEXEC);
    if (s_fd < 0) {
        int e = errno;
        snprintf(why, why_len, "cannot open %s: %s%s", s_path, strerror(e),
                 (e == EACCES || e == EPERM) ? " (the user needs the video group, or root)" : "");
        return false;
    }
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    if (ioctl(s_fd, FBIOGET_VSCREENINFO, &var) != 0 || ioctl(s_fd, FBIOGET_FSCREENINFO, &fix) != 0) {
        snprintf(why, why_len, "%s is not a framebuffer: %s", s_path, strerror(errno));
        close(s_fd);
        s_fd = -1;
        return false;
    }
    size_t offset = 0;
    if (!native_fbdev_surface(&var, &fix, m->display.rotate, &s_surface, &offset, why, why_len)) {
        close(s_fd);
        s_fd = -1;
        return false;
    }
    s_map_len = fix.smem_len ? fix.smem_len : offset + (size_t)fix.line_length * var.yres;
    s_map = mmap(NULL, s_map_len, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, 0);
    if (s_map == MAP_FAILED) {
        snprintf(why, why_len, "cannot map %s: %s", s_path, strerror(errno));
        s_map = NULL;
        close(s_fd);
        s_fd = -1;
        return false;
    }
    s_surface.pixels = s_map + offset;
    s_scale = m->display.scale;
    memset(s_map, 0, s_map_len);
    fprintf(stderr, "native: fbdev %s (%s): %dx%d %s, %d bytes per row, rotate %d\n",
            s_path, fix.id, s_surface.w, s_surface.h,
            s_surface.format == FMRB_PRESENT_RGB565 ? "RGB565" : "XRGB8888",
            s_surface.stride_bytes, s_surface.rotate);
    return true;
}

static bool fbdev_pump(int wait_ms)
{
    poll(NULL, 0, wait_ms);
    return false;
}

static void fbdev_present(const uint16_t *px, int w, int h, bool changed)
{
    if (!changed || s_map == NULL) return;
    fmrb_present_place_t place;
    fmrb_present_place(&s_surface, w, h, s_scale, &place);
    fmrb_present_draw(px, w, h, &s_surface, &place);
}

static void fbdev_close(void)
{
    if (s_map != NULL) {
        memset(s_map, 0, s_map_len);
        munmap(s_map, s_map_len);
        s_map = NULL;
    }
    if (s_fd >= 0) close(s_fd);
    s_fd = -1;
}

const native_output_t native_output_fbdev = {
    .name    = "fbdev",
    .open    = fbdev_open,
    .pump    = fbdev_pump,
    .present = fbdev_present,
    .close   = fbdev_close,
};
