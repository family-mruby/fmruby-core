/*
 * native_frame.c - see native_frame.h.
 *
 * Plain libc allocation on purpose: the buffer is read by the main thread,
 * which is not a FreeRTOS task and must not take the fmrb_mem pools' locks.
 */

#include "native_frame.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The fullscreen high-resolution frame (doc/fullscreen_hires/). */
#define NATIVE_FRAME_HIRES_W 640
#define NATIVE_FRAME_HIRES_H 360

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static uint16_t *s_px = NULL;
static int s_cap_px = 0;
static int s_w = 0;
static int s_h = 0;
static int s_base_w = 0;
static int s_base_h = 0;
static volatile uint32_t s_seq = 0;
static int s_wake_fd = -1;
/* The view of the frame on hand (0x0: all of it), and the hook deciding it. */
static int s_view[4];
static native_frame_view_fn s_view_fn = NULL;
/* The view of the frame each thread copied last. */
static __thread int t_copied_view[4];

void native_frame_set_view_fn(native_frame_view_fn fn)
{
    s_view_fn = fn;
}

void native_frame_set_wake_fd(int fd)
{
    s_wake_fd = fd;
}

/* After the lock is let go: the waiting thread copies under it. A full
 * counter (EAGAIN) still wakes the reader, so the result is not looked at. */
static void wake(void)
{
    if (s_wake_fd >= 0) {
        uint64_t one = 1;
        ssize_t n = write(s_wake_fd, &one, sizeof(one));
        (void)n;
    }
}

bool native_frame_init(int base_w, int base_h)
{
    int cap = base_w * base_h;
    if (cap < NATIVE_FRAME_HIRES_W * NATIVE_FRAME_HIRES_H) {
        cap = NATIVE_FRAME_HIRES_W * NATIVE_FRAME_HIRES_H;
    }
    pthread_mutex_lock(&s_lock);
    if (cap > s_cap_px) {
        uint16_t *px = (uint16_t *)calloc((size_t)cap, sizeof(uint16_t));
        if (px == NULL) {
            pthread_mutex_unlock(&s_lock);
            return false;
        }
        free(s_px);
        s_px = px;
        s_cap_px = cap;
    }
    s_base_w = base_w;
    s_base_h = base_h;
    pthread_mutex_unlock(&s_lock);
    return true;
}

static void put_view(const uint16_t *fb, int w, int h, const int view[4])
{
    if (fb == NULL || w <= 0 || h <= 0) return;
    pthread_mutex_lock(&s_lock);
    if (s_px != NULL && w * h <= s_cap_px) {
        memcpy(s_px, fb, (size_t)w * (size_t)h * sizeof(uint16_t));
        s_w = w;
        s_h = h;
        memcpy(s_view, view, sizeof(s_view));
        s_seq++;
    }
    pthread_mutex_unlock(&s_lock);
    wake();
}

void native_frame_put(const uint16_t *fb, int w, int h)
{
    static const int all[4] = { 0, 0, 0, 0 };
    put_view(fb, w, h, all);
}

void native_frame_put_canvases(const uint16_t *fb, int w, int h, const native_frame_canvas_t *c, int n)
{
    int view[4] = { 0, 0, 0, 0 };
    native_frame_view_fn fn = s_view_fn;
    if (fn == NULL || !fn(c, n, w, h, view)) memset(view, 0, sizeof(view));
    put_view(fb, w, h, view);
}

void native_frame_put_rect(const uint16_t *block, int x0, int y0, int w, int h,
                           int fb_w, int fb_h)
{
    if (block == NULL || w <= 0 || h <= 0) return;
    pthread_mutex_lock(&s_lock);
    if (s_px != NULL && fb_w == s_w && fb_h == s_h &&
        x0 >= 0 && y0 >= 0 && x0 + w <= s_w && y0 + h <= s_h) {
        for (int y = 0; y < h; y++) {
            memcpy(s_px + (size_t)(y0 + y) * s_w + x0,
                   block + (size_t)y * w, (size_t)w * sizeof(uint16_t));
        }
        s_seq++;
    }
    pthread_mutex_unlock(&s_lock);
    wake();
}

uint32_t native_frame_seq(void)
{
    return s_seq;
}

void native_frame_base_size(int *w, int *h)
{
    pthread_mutex_lock(&s_lock);
    *w = s_base_w;
    *h = s_base_h;
    pthread_mutex_unlock(&s_lock);
}

void native_frame_size(int *w, int *h)
{
    pthread_mutex_lock(&s_lock);
    *w = s_w;
    *h = s_h;
    pthread_mutex_unlock(&s_lock);
}

bool native_frame_copy(uint16_t *dst, int cap_px, int *w, int *h, uint32_t *seq)
{
    bool ok = false;
    pthread_mutex_lock(&s_lock);
    if (s_px != NULL && s_w > 0 && s_h > 0 && s_w * s_h <= cap_px) {
        memcpy(dst, s_px, (size_t)s_w * (size_t)s_h * sizeof(uint16_t));
        *w = s_w;
        *h = s_h;
        *seq = s_seq;
        memcpy(t_copied_view, s_view, sizeof(t_copied_view));
        ok = true;
    }
    pthread_mutex_unlock(&s_lock);
    return ok;
}

void native_frame_copied_view(int view[4])
{
    memcpy(view, t_copied_view, sizeof(t_copied_view));
}
