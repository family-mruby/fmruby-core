/*
 * output_drm.c - DRM/KMS as an output (native_backends.h): HDMI on the
 * Cardputer Zero, the console of a Linux PC. Built when libdrm is found
 * (NATIVE_HAVE_DRM).
 *
 *   card       display.device: "auto" tries /dev/dri/card0..15 and takes the
 *              first with a connected output; a path takes that card
 *   mode       display.mode "WxH" or "WxH@Hz" when the connector offers it,
 *              otherwise (and for "auto") the connector's preferred mode
 *   buffers    two XRGB8888 dumb buffers, drawn through the present core and
 *              shown with a page flip; the main loop's wait is a poll on the
 *              card for the flip's completion (pump)
 *   master     setting a mode needs the card's master role. A desktop or a
 *              compositor already holding it (or a session that is not on
 *              the console) is a refusal with that reason, not a retry
 *
 * The CRTC is put back as it was when the program ends.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "fmrb_present.h"
#include "native_backends.h"

typedef struct {
    uint32_t handle, fb_id, pitch;
    uint64_t size;
    uint8_t *map;
} dumb_t;

static int s_fd = -1;
static uint32_t s_conn_id = 0, s_crtc_id = 0;
static drmModeModeInfo s_mode;
static drmModeCrtc *s_saved = NULL;
static dumb_t s_buf[2];
static int s_front = 0;
static bool s_flip_pending = false;
static bool s_dirty = false;
static int s_scale = 0, s_rotate = 0;
static char s_card[64];

static const char *conn_type(uint32_t t)
{
    switch (t) {
    case DRM_MODE_CONNECTOR_HDMIA: return "HDMI-A";
    case DRM_MODE_CONNECTOR_HDMIB: return "HDMI-B";
    case DRM_MODE_CONNECTOR_DSI:   return "DSI";
    case DRM_MODE_CONNECTOR_eDP:   return "eDP";
    case DRM_MODE_CONNECTOR_DisplayPort: return "DP";
    case DRM_MODE_CONNECTOR_VIRTUAL: return "Virtual";
    case DRM_MODE_CONNECTOR_DPI:   return "DPI";
    case DRM_MODE_CONNECTOR_Composite: return "Composite";
    default: return "other";
    }
}

static void destroy_dumb(dumb_t *b)
{
    if (b->map != NULL) munmap(b->map, b->size);
    if (b->fb_id != 0) drmModeRmFB(s_fd, b->fb_id);
    if (b->handle != 0) {
        struct drm_mode_destroy_dumb d = { .handle = b->handle };
        drmIoctl(s_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    }
    memset(b, 0, sizeof(*b));
}

static bool create_dumb(dumb_t *b, int w, int h, char *why, size_t why_len)
{
    memset(b, 0, sizeof(*b));
    struct drm_mode_create_dumb c = { .width = (uint32_t)w, .height = (uint32_t)h, .bpp = 32 };
    if (drmIoctl(s_fd, DRM_IOCTL_MODE_CREATE_DUMB, &c) != 0) {
        snprintf(why, why_len, "%s: cannot create a %dx%d buffer: %s", s_card, w, h, strerror(errno));
        return false;
    }
    b->handle = c.handle;
    b->pitch = c.pitch;
    b->size = c.size;
    if (drmModeAddFB(s_fd, (uint32_t)w, (uint32_t)h, 24, 32, b->pitch, b->handle, &b->fb_id) != 0) {
        snprintf(why, why_len, "%s: cannot add the frame buffer: %s", s_card, strerror(errno));
        destroy_dumb(b);
        return false;
    }
    struct drm_mode_map_dumb mreq = { .handle = b->handle };
    if (drmIoctl(s_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) != 0) {
        snprintf(why, why_len, "%s: cannot map the buffer: %s", s_card, strerror(errno));
        destroy_dumb(b);
        return false;
    }
    b->map = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, mreq.offset);
    if (b->map == MAP_FAILED) {
        b->map = NULL;
        snprintf(why, why_len, "%s: cannot map the buffer: %s", s_card, strerror(errno));
        destroy_dumb(b);
        return false;
    }
    memset(b->map, 0, b->size);
    return true;
}

/* The mode the definition asks for, else the preferred one, else the first. */
static const drmModeModeInfo *pick_mode(const drmModeConnector *c, const char *want, bool *matched)
{
    int w = 0, h = 0, hz = 0;
    *matched = false;
    if (strcmp(want, "auto") != 0 && sscanf(want, "%dx%d@%d", &w, &h, &hz) >= 2) {
        for (int i = 0; i < c->count_modes; i++) {
            const drmModeModeInfo *m = &c->modes[i];
            if (m->hdisplay == w && m->vdisplay == h && (hz == 0 || (int)m->vrefresh == hz)) {
                *matched = true;
                return m;
            }
        }
    }
    for (int i = 0; i < c->count_modes; i++) {
        if (c->modes[i].type & DRM_MODE_TYPE_PREFERRED) return &c->modes[i];
    }
    return c->count_modes > 0 ? &c->modes[0] : NULL;
}

static uint32_t pick_crtc(const drmModeRes *res, const drmModeConnector *c)
{
    if (c->encoder_id != 0) {
        drmModeEncoder *e = drmModeGetEncoder(s_fd, c->encoder_id);
        if (e != NULL) {
            uint32_t crtc = e->crtc_id;
            drmModeFreeEncoder(e);
            if (crtc != 0) return crtc;
        }
    }
    for (int i = 0; i < c->count_encoders; i++) {
        drmModeEncoder *e = drmModeGetEncoder(s_fd, c->encoders[i]);
        if (e == NULL) continue;
        for (int k = 0; k < res->count_crtcs; k++) {
            if (e->possible_crtcs & (1u << k)) {
                uint32_t crtc = res->crtcs[k];
                drmModeFreeEncoder(e);
                return crtc;
            }
        }
        drmModeFreeEncoder(e);
    }
    return 0;
}

/* One card: a connected output, a mode, a CRTC and the master role. */
static bool try_card(const char *path, const native_machine_t *m, char *why, size_t why_len)
{
    snprintf(s_card, sizeof(s_card), "%s", path);
    s_fd = open(path, O_RDWR | O_CLOEXEC);
    if (s_fd < 0) {
        int e = errno;
        snprintf(why, why_len, "cannot open %s: %s%s", path, strerror(e),
                 (e == EACCES || e == EPERM) ? " (the user needs the video group, or root)" : "");
        return false;
    }
    uint64_t dumb = 0;
    if (drmGetCap(s_fd, DRM_CAP_DUMB_BUFFER, &dumb) != 0 || dumb == 0) {
        snprintf(why, why_len, "%s has no dumb buffers (not a display controller)", path);
        goto fail;
    }
    drmModeRes *res = drmModeGetResources(s_fd);
    if (res == NULL) {
        snprintf(why, why_len, "%s has no mode setting (KMS): %s", path, strerror(errno));
        goto fail;
    }
    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors && conn == NULL; i++) {
        drmModeConnector *c = drmModeGetConnector(s_fd, res->connectors[i]);
        if (c == NULL) continue;
        if (c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            conn = c;
        } else {
            drmModeFreeConnector(c);
        }
    }
    if (conn == NULL) {
        snprintf(why, why_len, "%s: no connected output among %d connectors (is the cable in?)",
                 path, res->count_connectors);
        drmModeFreeResources(res);
        goto fail;
    }
    bool matched = false;
    const drmModeModeInfo *mode = pick_mode(conn, m->display.mode, &matched);
    s_crtc_id = pick_crtc(res, conn);
    drmModeFreeResources(res);
    if (mode == NULL || s_crtc_id == 0) {
        snprintf(why, why_len, "%s: %s-%u has no usable %s", path, conn_type(conn->connector_type),
                 conn->connector_type_id, mode == NULL ? "mode" : "CRTC");
        drmModeFreeConnector(conn);
        goto fail;
    }
    s_mode = *mode;
    s_conn_id = conn->connector_id;
    fprintf(stderr, "native: drm %s %s-%u: mode %dx%d@%u%s\n", path, conn_type(conn->connector_type),
            conn->connector_type_id, s_mode.hdisplay, s_mode.vdisplay, s_mode.vrefresh,
            matched ? "" : (strcmp(m->display.mode, "auto") == 0 ? " (preferred)"
                                                                  : " (preferred; the asked mode is not offered)"));
    drmModeFreeConnector(conn);

    if (!drmIsMaster(s_fd) && drmSetMaster(s_fd) != 0) {
        snprintf(why, why_len, "cannot become the DRM master of %s (%s): another program -- a desktop, "
                               "a compositor, X -- holds the display, or this session is not on the console",
                 path, strerror(errno));
        goto fail;
    }
    return true;
fail:
    close(s_fd);
    s_fd = -1;
    return false;
}

static bool drm_open(const native_machine_t *m, char *why, size_t why_len)
{
    s_scale = m->display.scale;
    s_rotate = m->display.rotate;
    bool ok = false;
    if (strcmp(m->display.device, "auto") == 0) {
        char first[300] = { 0 };
        int tried = 0;
        for (int i = 0; i < 16 && !ok; i++) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/dri/card%d", i);
            if (access(path, F_OK) != 0) continue;
            tried++;
            ok = try_card(path, m, why, why_len);
            if (!ok && first[0] == '\0') snprintf(first, sizeof(first), "%s", why);
        }
        if (!ok) {
            if (tried == 0) snprintf(why, why_len, "no DRM device: /dev/dri has no card*");
            else if (tried > 1) snprintf(why, why_len, "%s (and %d more cards tried)", first, tried - 1);
            return false;
        }
    } else if (!try_card(m->display.device, m, why, why_len)) {
        return false;
    }

    for (int i = 0; i < 2; i++) {
        if (!create_dumb(&s_buf[i], s_mode.hdisplay, s_mode.vdisplay, why, why_len)) {
            destroy_dumb(&s_buf[0]);
            drmDropMaster(s_fd);
            close(s_fd);
            s_fd = -1;
            return false;
        }
    }
    s_saved = drmModeGetCrtc(s_fd, s_crtc_id);
    s_front = 0;
    if (drmModeSetCrtc(s_fd, s_crtc_id, s_buf[0].fb_id, 0, 0, &s_conn_id, 1, &s_mode) != 0) {
        snprintf(why, why_len, "%s: cannot set the mode %dx%d: %s", s_card, s_mode.hdisplay,
                 s_mode.vdisplay, strerror(errno));
        destroy_dumb(&s_buf[0]);
        destroy_dumb(&s_buf[1]);
        drmDropMaster(s_fd);
        close(s_fd);
        s_fd = -1;
        return false;
    }
    return true;
}

static void on_flip(int fd, unsigned seq, unsigned sec, unsigned usec, void *ud)
{
    (void)fd; (void)seq; (void)sec; (void)usec; (void)ud;
    s_flip_pending = false;
}

static bool drm_pump(int wait_ms)
{
    struct pollfd p = { .fd = s_fd, .events = POLLIN };
    if (poll(&p, 1, wait_ms) > 0 && (p.revents & POLLIN)) {
        drmEventContext ctx = { .version = 2, .page_flip_handler = on_flip };
        drmHandleEvent(s_fd, &ctx);
    }
    return false;
}

static void drm_present(const uint16_t *px, int w, int h, bool changed)
{
    if (s_fd < 0) return;
    s_dirty |= changed;
    if (!s_dirty || s_flip_pending) return;
    int back = 1 - s_front;
    dumb_t *b = &s_buf[back];
    fmrb_present_surface_t surface = {
        .pixels = b->map, .w = s_mode.hdisplay, .h = s_mode.vdisplay,
        .stride_bytes = (int)b->pitch, .format = FMRB_PRESENT_XRGB8888, .rotate = s_rotate,
    };
    fmrb_present_place_t place;
    fmrb_present_place(&surface, w, h, s_scale, &place);
    fmrb_present_draw(px, w, h, &surface, &place);
    s_dirty = false;
    if (drmModePageFlip(s_fd, s_crtc_id, b->fb_id, DRM_MODE_PAGE_FLIP_EVENT, NULL) == 0) {
        s_flip_pending = true;
    } else {
        /* No flip (some drivers): show it now. */
        drmModeSetCrtc(s_fd, s_crtc_id, b->fb_id, 0, 0, &s_conn_id, 1, &s_mode);
    }
    s_front = back;
}

static void drm_close(void)
{
    if (s_fd < 0) return;
    if (s_saved != NULL) {
        if (s_saved->mode_valid) {
            drmModeSetCrtc(s_fd, s_saved->crtc_id, s_saved->buffer_id, s_saved->x, s_saved->y,
                           &s_conn_id, 1, &s_saved->mode);
        } else {
            drmModeSetCrtc(s_fd, s_saved->crtc_id, 0, 0, 0, NULL, 0, NULL);
        }
        drmModeFreeCrtc(s_saved);
        s_saved = NULL;
    }
    destroy_dumb(&s_buf[0]);
    destroy_dumb(&s_buf[1]);
    drmDropMaster(s_fd);
    close(s_fd);
    s_fd = -1;
}

const native_output_t native_output_drm = {
    .name    = "drm",
    .open    = drm_open,
    .pump    = drm_pump,
    .present = drm_present,
    .close   = drm_close,
};
