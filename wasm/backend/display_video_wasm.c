/*
 * display_video_wasm.c - display_p4_video.h for the wasm build.
 *
 * The P4 does this with its hardware JPEG engine (main/drivers/display_p4/
 * display_p4_video.cpp). There is no such engine in a browser, but there is
 * already a decoder in the binary: LovyanGFX brings TJpgDec along
 * (lgfx/utility/lgfx_tjpgd.c, compiled into lgfx_wasm), so playback needs no
 * new dependency -- only the software equivalent of what the P4 does in
 * silicon.
 *
 * Everything on the far side of this file is shared and unchanged. The
 * display task's video_service() takes a frame, copies its rows into the
 * canvas and commits it, and it neither knows nor cares which decoder filled
 * the buffer. So the contract here is copied exactly from the P4 player: two
 * slots, the newest ready one wins, RGB565 little-endian pixels, stride in
 * pixels.
 *
 * What is deliberately different:
 *
 *   - No 16-pixel alignment. That grid is the hardware decoder's; TJpgDec
 *     hands back whatever the picture is, so stride_px is simply the width.
 *   - Plain malloc. No DMA, no alignment, no separate decoder allocator.
 *   - RGB888 -> RGB565 in the output callback. lgfx builds TJpgDec with
 *     JD_FORMAT 0, so it emits three bytes a pixel and the pack happens here
 *     rather than in the decoder.
 *
 * On not getting in the way, which is the thing to watch on a machine with
 * one core and a software decoder:
 *
 *   - The task runs at FMRB_VIDEO_P4_TASK_PRIORITY, the same as on the P4 and
 *     below the display task, so compositing and everything above it keep
 *     their turn.
 *   - It sleeps in VIDEO_WAIT_SLICE_MS hops rather than for a whole frame
 *     interval, so it yields often and notices stop/pause quickly.
 *   - When decoding cannot keep up -- likely here, where the P4 spends
 *     microseconds and this spends milliseconds -- the loop reads frames past
 *     without decoding them and gives up its debt after four intervals. The
 *     picture drops frames; it does not take the CPU to hold a schedule it
 *     cannot make.
 *   - No task exists unless a video is open. It is created by video_open and
 *     deletes itself on stop.
 */

#include "display_p4_video.h"

#include "fmrb_hal_time.h"
#include "fmrb_link_protocol.h"
#include "fmrb_log.h"
#include "fmrb_task_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lgfx/utility/lgfx_tjpgd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "wasm_video";

/* A frame larger than this is either not ours or a broken file. */
#define VIDEO_MAX_FRAME_BYTES (512 * 1024)
/* Block size for reading the file: a frame is a few KB, so this turns one
 * frame into a couple of reads instead of thousands. */
#define VIDEO_IO_BUF_BYTES    (16 * 1024)
/* Longest the player sleeps in one go, and so the worst case for noticing a
 * stop or a pause. */
#define VIDEO_WAIT_SLICE_MS   20
/* Largest picture we will play. The framebuffer can be resized by the page up
 * to 1920x1080, but a software decoder has no business filling that at a
 * frame rate; this is the size the feature is documented at with room over. */
#define VIDEO_MAX_W 640
#define VIDEO_MAX_H 480
/* TJpgDec's working pool. 3100 bytes is the classic figure for a 3-component
 * image; this is rounded up rather than tuned, because it is allocated once
 * per decode and freed again. */
#define JPEG_WORK_BYTES 8192

/* ============================================================
 * TJpgDec wrapper: one JPEG in memory -> RGB565 in a caller's buffer
 * ============================================================ */

typedef struct {
    const uint8_t *src;
    size_t         src_len;
    size_t         src_pos;
    uint8_t       *dst;         /* RGB565, dst_stride_px pixels a row */
    size_t         dst_size;
    uint16_t       dst_stride_px;
    uint16_t       dst_h;
} jpeg_ctx_t;

/* TJpgDec input: hand over the next bytes, or skip them when buf is NULL. */
static uint32_t jpeg_in(void *dev, uint8_t *buf, uint32_t len)
{
    jpeg_ctx_t *c = (jpeg_ctx_t *)dev;
    size_t left = c->src_len - c->src_pos;
    if (len > left) len = (uint32_t)left;
    if (buf) memcpy(buf, c->src + c->src_pos, len);
    c->src_pos += len;
    return len;
}

/* TJpgDec output: one rectangle of RGB888, packed into the RGB565 buffer.
 * Clipped rather than trusted -- a file that lies about its size must not be
 * able to write past the slot. */
static uint32_t jpeg_out(void *dev, void *bitmap, JRECT *rect)
{
    jpeg_ctx_t *c = (jpeg_ctx_t *)dev;
    const uint8_t *src = (const uint8_t *)bitmap;
    uint32_t rw = rect->right - rect->left + 1;

    for (uint32_t y = rect->top; y <= rect->bottom; y++) {
        if (y >= c->dst_h) break;
        uint16_t *row = (uint16_t *)c->dst + (size_t)y * c->dst_stride_px;
        const uint8_t *s = src + (size_t)(y - rect->top) * rw * 3;
        for (uint32_t x = rect->left; x <= rect->right; x++) {
            if (x < c->dst_stride_px) {
                row[x] = (uint16_t)(((s[0] & 0xF8) << 8) |
                                    ((s[1] & 0xFC) << 3) |
                                     (s[2] >> 3));
            }
            s += 3;
        }
    }
    return 1;  /* keep going */
}

/* Decode into dst. Returns false and leaves dst untouched on any failure. */
static bool jpeg_decode_into(const uint8_t *jpeg, size_t len,
                             uint8_t *dst, size_t dst_size,
                             uint16_t *out_w, uint16_t *out_h,
                             uint16_t *out_stride_px)
{
    if (!jpeg || !dst || len < 4) return false;

    uint8_t *work = (uint8_t *)malloc(JPEG_WORK_BYTES);
    if (!work) {
        FMRB_LOGE(TAG, "no memory for the decoder pool");
        return false;
    }

    jpeg_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.src = jpeg;
    ctx.src_len = len;

    lgfxJdec jd;
    JRESULT r = lgfx_jd_prepare(&jd, jpeg_in, work, JPEG_WORK_BYTES, &ctx);
    if (r != JDR_OK) {
        FMRB_LOGE(TAG, "jd_prepare failed (%d)", (int)r);
        free(work);
        return false;
    }
    if (jd.width == 0 || jd.height == 0) {
        free(work);
        return false;
    }
    if ((size_t)jd.width * jd.height * 2u > dst_size) {
        FMRB_LOGE(TAG, "%ux%u does not fit the %u byte buffer",
                  (unsigned)jd.width, (unsigned)jd.height, (unsigned)dst_size);
        free(work);
        return false;
    }

    ctx.dst           = dst;
    ctx.dst_size      = dst_size;
    ctx.dst_stride_px = jd.width;
    ctx.dst_h         = jd.height;

    r = lgfx_jd_decomp(&jd, jpeg_out, 0);
    free(work);
    if (r != JDR_OK) {
        FMRB_LOGE(TAG, "jd_decomp failed (%d)", (int)r);
        return false;
    }

    if (out_w)         *out_w = jd.width;
    if (out_h)         *out_h = jd.height;
    if (out_stride_px) *out_stride_px = jd.width;
    return true;
}

/* Read just the header, for the size, without decoding any pixels. */
static bool jpeg_probe_size(const uint8_t *jpeg, size_t len,
                            uint16_t *out_w, uint16_t *out_h)
{
    uint8_t *work = (uint8_t *)malloc(JPEG_WORK_BYTES);
    if (!work) return false;

    jpeg_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.src = jpeg;
    ctx.src_len = len;

    lgfxJdec jd;
    JRESULT r = lgfx_jd_prepare(&jd, jpeg_in, work, JPEG_WORK_BYTES, &ctx);
    free(work);
    if (r != JDR_OK) return false;
    if (out_w) *out_w = jd.width;
    if (out_h) *out_h = jd.height;
    return true;
}

/* ============================================================
 * Still images: what display_p4_task.cpp calls for a .jpg
 * ============================================================ */

bool display_p4_jpeg_is_jpeg(const uint8_t *data, size_t len)
{
    return len >= 2 && data && data[0] == 0xFF && data[1] == 0xD8;
}

uint8_t *display_p4_jpeg_decode(const uint8_t *jpeg, size_t len,
                                uint16_t *out_w, uint16_t *out_h,
                                uint16_t *out_stride_px)
{
    uint16_t w = 0, h = 0;
    if (!jpeg_probe_size(jpeg, len, &w, &h) || w == 0 || h == 0) return NULL;

    size_t size = (size_t)w * h * 2u;
    uint8_t *out = (uint8_t *)malloc(size);
    if (!out) {
        FMRB_LOGE(TAG, "no memory for a %ux%u picture", (unsigned)w, (unsigned)h);
        return NULL;
    }
    memset(out, 0, size);
    if (!jpeg_decode_into(jpeg, len, out, size, out_w, out_h, out_stride_px)) {
        free(out);
        return NULL;
    }
    return out;
}

void display_p4_jpeg_free_output(uint8_t *buf) { free(buf); }

uint8_t *display_p4_jpeg_alloc_input(size_t size, size_t *out_alloc)
{
    uint8_t *p = (uint8_t *)malloc(size);
    if (out_alloc) *out_alloc = p ? size : 0;
    return p;
}

void display_p4_jpeg_free_input(uint8_t *buf) { free(buf); }

/* ============================================================
 * Frame slots -- same contract as the P4 player
 *
 * The player decodes into whichever slot is free and marks it ready; the
 * display task takes the ready one and returns it when the copy is done. A
 * frame that is still ready when a newer one lands is dropped: the newer
 * picture is the one worth showing.
 * ============================================================ */

typedef enum {
    SLOT_FREE = 0,
    SLOT_FILLING,
    SLOT_READY,
    SLOT_IN_USE,
} slot_state_t;

typedef struct {
    uint8_t     *buf;
    size_t       size;
    slot_state_t state;
    uint16_t     w, h, stride_px;
    uint32_t     seq;
} frame_slot_t;

#define VIDEO_SLOTS 2

typedef struct {
    /* Owned by the player task except where noted. */
    FILE    *fp;
    char     path[128];
    uint16_t canvas_id;
    int16_t  dst_x, dst_y;
    uint16_t fps;
    uint8_t  flags;
    uint16_t width, height, stride_px;

    uint8_t *read_buf;       /* JPEG bytes of the frame being decoded */
    size_t   read_buf_size;

    uint8_t *io_buf;         /* block reader over the file */
    size_t   io_len;
    size_t   io_pos;

    frame_slot_t slots[VIDEO_SLOTS];
    uint32_t     next_seq;

    /* Shared state (guarded by lock) */
    volatile uint8_t state;
    volatile bool    stop_req;
    volatile bool    rewind_req;
    uint32_t         frames_shown;
    uint32_t         frames_dropped;

    TaskHandle_t      task;
    SemaphoreHandle_t lock;
} video_player_t;

static video_player_t s_p;

static inline void player_lock(void)   { if (s_p.lock) xSemaphoreTake(s_p.lock, portMAX_DELAY); }
static inline void player_unlock(void) { if (s_p.lock) xSemaphoreGive(s_p.lock); }

/* ---- file reading --------------------------------------------------------- */

static inline int io_getc(void)
{
    if (s_p.io_pos >= s_p.io_len) {
        s_p.io_len = fread(s_p.io_buf, 1, VIDEO_IO_BUF_BYTES, s_p.fp);
        s_p.io_pos = 0;
        if (s_p.io_len == 0) return -1;
    }
    return s_p.io_buf[s_p.io_pos++];
}

static void player_seek_start(void)
{
    fseek(s_p.fp, 0, SEEK_SET);
    s_p.io_len = 0;
    s_p.io_pos = 0;
}

/* Copy the next JPEG frame (SOI..EOI inclusive) into dst. Returns its length,
 * or 0 at end of file / on a malformed stream. Scanning for FFD9 is safe for
 * the streams this targets: inside entropy-coded data every FF is stuffed as
 * FF 00, and restart markers are FFD0..FFD7. */
static size_t read_next_frame(uint8_t *dst, size_t cap)
{
    int c;
    int prev = -1;
    bool found = false;
    while ((c = io_getc()) >= 0) {
        if (prev == 0xFF && c == 0xD8) { found = true; break; }
        prev = c;
    }
    if (!found) return 0;
    if (cap < 2) return 0;

    dst[0] = 0xFF;
    dst[1] = 0xD8;
    size_t len = 2;

    prev = -1;
    while ((c = io_getc()) >= 0) {
        if (len >= cap) {
            FMRB_LOGE(TAG, "frame exceeds %u bytes, giving up", (unsigned)cap);
            return 0;
        }
        dst[len++] = (uint8_t)c;
        if (prev == 0xFF && c == 0xD9) return len;
        prev = c;
    }
    return 0;  /* truncated tail */
}

/* ---- slots ---------------------------------------------------------------- */

static frame_slot_t *slot_take_for_write(void)
{
    frame_slot_t *pick = NULL;
    player_lock();
    for (int i = 0; i < VIDEO_SLOTS; i++) {
        if (s_p.slots[i].state == SLOT_FREE) { pick = &s_p.slots[i]; break; }
    }
    if (!pick) {
        for (int i = 0; i < VIDEO_SLOTS; i++) {
            if (s_p.slots[i].state == SLOT_READY) { pick = &s_p.slots[i]; break; }
        }
    }
    if (pick) pick->state = SLOT_FILLING;
    player_unlock();
    return pick;
}

static void slot_publish(frame_slot_t *slot)
{
    player_lock();
    slot->seq = ++s_p.next_seq;
    slot->state = SLOT_READY;
    player_unlock();
}

static void slot_abandon(frame_slot_t *slot)
{
    player_lock();
    slot->state = SLOT_FREE;
    player_unlock();
}

bool display_p4_video_take_frame(const uint8_t **pixels, uint16_t *w,
                                 uint16_t *h, uint16_t *stride_px,
                                 uint16_t *canvas_id, int16_t *x, int16_t *y)
{
    bool got = false;
    player_lock();
    frame_slot_t *newest = NULL;
    for (int i = 0; i < VIDEO_SLOTS; i++) {
        if (s_p.slots[i].state != SLOT_READY) continue;
        if (!newest || s_p.slots[i].seq > newest->seq) newest = &s_p.slots[i];
    }
    if (newest) {
        for (int i = 0; i < VIDEO_SLOTS; i++) {
            if (&s_p.slots[i] != newest && s_p.slots[i].state == SLOT_READY) {
                s_p.slots[i].state = SLOT_FREE;
                s_p.frames_dropped++;
            }
        }
        newest->state = SLOT_IN_USE;
        if (pixels)    *pixels = newest->buf;
        if (w)         *w = newest->w;
        if (h)         *h = newest->h;
        if (stride_px) *stride_px = newest->stride_px;
        if (canvas_id) *canvas_id = s_p.canvas_id;
        if (x) *x = s_p.dst_x;
        if (y) *y = s_p.dst_y;
        s_p.frames_shown++;
        got = true;
    }
    player_unlock();
    return got;
}

void display_p4_video_release_frame(void)
{
    player_lock();
    for (int i = 0; i < VIDEO_SLOTS; i++) {
        if (s_p.slots[i].state == SLOT_IN_USE) s_p.slots[i].state = SLOT_FREE;
    }
    player_unlock();
}

/* ---- teardown ------------------------------------------------------------- */

static void player_free_buffers(void)
{
    for (int i = 0; i < VIDEO_SLOTS; i++) {
        free(s_p.slots[i].buf);
        s_p.slots[i].buf = NULL;
        s_p.slots[i].size = 0;
        s_p.slots[i].state = SLOT_FREE;
    }
    free(s_p.read_buf);
    s_p.read_buf = NULL;
    s_p.read_buf_size = 0;
    free(s_p.io_buf);
    s_p.io_buf = NULL;
    s_p.io_len = 0;
    s_p.io_pos = 0;
}

static void player_close_file(void)
{
    if (s_p.fp) {
        fclose(s_p.fp);
        s_p.fp = NULL;
    }
    player_free_buffers();
    player_lock();
    s_p.state = FMRB_LINK_GFX_VIDEO_STATE_IDLE;
    s_p.canvas_id = 0;
    player_unlock();
}

/* ---- player task ---------------------------------------------------------- */

/* Decode one frame into a slot. 1 = published, 0 = end of file, -1 = fatal. */
static int player_step(bool decode)
{
    size_t len = read_next_frame(s_p.read_buf, s_p.read_buf_size);
    if (len == 0) return 0;
    if (!decode) return 1;

    frame_slot_t *slot = slot_take_for_write();
    if (!slot) return 1;

    uint16_t w = 0, h = 0, stride = 0;
    if (!jpeg_decode_into(s_p.read_buf, len, slot->buf, slot->size,
                          &w, &h, &stride)) {
        slot_abandon(slot);
        return -1;
    }
    slot->w = w;
    slot->h = h;
    slot->stride_px = stride;
    slot_publish(slot);
    return 1;
}

static void player_task(void *arg)
{
    (void)arg;
    const int64_t interval_us = 1000000 / (s_p.fps ? s_p.fps : 15);
    int64_t next_due = (int64_t)fmrb_hal_time_get_us();

    while (1) {
        if (s_p.stop_req) break;

        if (s_p.state != FMRB_LINK_GFX_VIDEO_STATE_PLAYING) {
            vTaskDelay(pdMS_TO_TICKS(20));
            next_due = (int64_t)fmrb_hal_time_get_us();
            continue;
        }

        if (s_p.rewind_req) {
            s_p.rewind_req = false;
            player_seek_start();
            next_due = (int64_t)fmrb_hal_time_get_us();
        }

        int64_t now = (int64_t)fmrb_hal_time_get_us();
        if (now < next_due) {
            /* Short hops, not one long sleep: stop, pause and rewind are only
             * noticed at the top of this loop. */
            int64_t wait_ms = (next_due - now) / 1000;
            if (wait_ms > VIDEO_WAIT_SLICE_MS) wait_ms = VIDEO_WAIT_SLICE_MS;
            vTaskDelay(wait_ms > 0 ? pdMS_TO_TICKS(wait_ms) : 1);
            continue;
        }

        /* Software decoding falls behind far more easily than the P4's engine
         * does, and insisting on the old schedule would mean skipping the rest
         * of the file. Past four intervals, give up the debt and play on. */
        if ((now - next_due) > 4 * interval_us) next_due = now;

        /* More than one interval late: read the frame past without decoding
         * it. The clock is what the viewer notices, not the missing picture --
         * and this is what keeps a slow decode from taking the machine over. */
        bool late = (now - next_due) > interval_us;
        int r = player_step(!late);
        if (late) {
            player_lock();
            s_p.frames_dropped++;
            player_unlock();
        }

        if (r == 0) {
            if (s_p.flags & FMRB_LINK_GFX_VIDEO_FLAG_LOOP) {
                player_seek_start();
                next_due = (int64_t)fmrb_hal_time_get_us();
                continue;
            }
            player_lock();
            s_p.state = FMRB_LINK_GFX_VIDEO_STATE_FINISHED;
            player_unlock();
            FMRB_LOGI(TAG, "playback finished: %u shown, %u dropped",
                      (unsigned)s_p.frames_shown, (unsigned)s_p.frames_dropped);
            continue;
        }
        if (r < 0) {
            FMRB_LOGE(TAG, "decode error, stopping playback");
            break;
        }

        next_due += interval_us;
    }

    player_close_file();
    s_p.stop_req = false;
    s_p.task = NULL;
    vTaskDelete(NULL);
}

/* ---- public API ----------------------------------------------------------- */

static void player_stop_and_wait(void)
{
    if (s_p.task) {
        s_p.stop_req = true;
        for (int i = 0; i < 200 && s_p.task; i++) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (s_p.task) FMRB_LOGW(TAG, "player task did not stop in time");
    } else if (s_p.fp) {
        player_close_file();
    }
}

fmrb_err_t display_p4_video_open(uint16_t canvas_id, int16_t x, int16_t y,
                                 const char *path, uint16_t fps, uint8_t flags,
                                 uint16_t *out_w, uint16_t *out_h)
{
    if (!path || !*path) return FMRB_ERR_INVALID_PARAM;
    if (fps == 0 || fps > 60) fps = 15;

    if (!s_p.lock) {
        s_p.lock = xSemaphoreCreateMutex();
        if (!s_p.lock) return FMRB_ERR_NO_MEMORY;
    }

    player_stop_and_wait();

    s_p.fp = fopen(path, "rb");
    if (!s_p.fp) {
        FMRB_LOGE(TAG, "cannot open %s", path);
        return FMRB_ERR_NOT_FOUND;
    }
    snprintf(s_p.path, sizeof(s_p.path), "%s", path);
    s_p.canvas_id      = canvas_id;
    s_p.dst_x          = x;
    s_p.dst_y          = y;
    s_p.fps            = fps;
    s_p.flags          = flags;
    s_p.frames_shown   = 0;
    s_p.frames_dropped = 0;
    s_p.next_seq       = 0;
    s_p.stop_req       = false;
    s_p.rewind_req     = false;

    s_p.read_buf = display_p4_jpeg_alloc_input(VIDEO_MAX_FRAME_BYTES,
                                               &s_p.read_buf_size);
    s_p.io_buf   = (uint8_t *)malloc(VIDEO_IO_BUF_BYTES);
    s_p.io_len   = 0;
    s_p.io_pos   = 0;
    if (!s_p.read_buf || !s_p.io_buf) {
        player_close_file();
        return FMRB_ERR_NO_MEMORY;
    }

    /* Learn the picture size from the first frame before allocating the
     * slots, then rewind so playback starts at frame 0. */
    size_t first_len = read_next_frame(s_p.read_buf, s_p.read_buf_size);
    if (first_len == 0) {
        FMRB_LOGE(TAG, "%s holds no JPEG frame", path);
        player_close_file();
        return FMRB_ERR_INVALID_PARAM;
    }
    uint16_t fw = 0, fh = 0;
    if (!jpeg_probe_size(s_p.read_buf, first_len, &fw, &fh) ||
        fw == 0 || fh == 0 || fw > VIDEO_MAX_W || fh > VIDEO_MAX_H) {
        FMRB_LOGE(TAG, "unsupported frame size %ux%u (max %ux%u)",
                  (unsigned)fw, (unsigned)fh, VIDEO_MAX_W, VIDEO_MAX_H);
        player_close_file();
        return FMRB_ERR_INVALID_PARAM;
    }
    player_seek_start();

    s_p.width     = fw;
    s_p.height    = fh;
    s_p.stride_px = fw;   /* no hardware grid to round up to */

    size_t slot_size = (size_t)s_p.stride_px * s_p.height * 2u;
    for (int i = 0; i < VIDEO_SLOTS; i++) {
        s_p.slots[i].buf   = (uint8_t *)malloc(slot_size);
        s_p.slots[i].size  = slot_size;
        s_p.slots[i].state = SLOT_FREE;
        if (!s_p.slots[i].buf) {
            FMRB_LOGE(TAG, "frame slot alloc failed (%u bytes)",
                      (unsigned)slot_size);
            player_close_file();
            return FMRB_ERR_NO_MEMORY;
        }
        memset(s_p.slots[i].buf, 0, slot_size);
    }

    player_lock();
    s_p.state = FMRB_LINK_GFX_VIDEO_STATE_PAUSED;
    player_unlock();

    BaseType_t ok = xTaskCreatePinnedToCore(player_task, "wasm_video",
                                            FMRB_VIDEO_P4_TASK_STACK_SIZE, NULL,
                                            FMRB_VIDEO_P4_TASK_PRIORITY,
                                            &s_p.task, FMRB_VIDEO_P4_TASK_CORE);
    if (ok != pdPASS) {
        s_p.task = NULL;
        player_close_file();
        return FMRB_ERR_NO_MEMORY;
    }

    FMRB_LOGI(TAG, "opened %s: %ux%u @%ufps -> canvas %u at (%d,%d)%s", path,
              s_p.width, s_p.height, fps, canvas_id, x, y,
              (flags & FMRB_LINK_GFX_VIDEO_FLAG_LOOP) ? " (loop)" : "");
    if (out_w) *out_w = s_p.width;
    if (out_h) *out_h = s_p.height;
    return FMRB_OK;
}

fmrb_err_t display_p4_video_control(uint8_t action)
{
    switch (action) {
    case FMRB_LINK_GFX_VIDEO_ACTION_PLAY:
        if (!s_p.fp) return FMRB_ERR_INVALID_STATE;
        player_lock();
        if (s_p.state == FMRB_LINK_GFX_VIDEO_STATE_FINISHED) s_p.rewind_req = true;
        s_p.state = FMRB_LINK_GFX_VIDEO_STATE_PLAYING;
        player_unlock();
        return FMRB_OK;

    case FMRB_LINK_GFX_VIDEO_ACTION_PAUSE:
        if (!s_p.fp) return FMRB_ERR_INVALID_STATE;
        player_lock();
        if (s_p.state == FMRB_LINK_GFX_VIDEO_STATE_PLAYING) {
            s_p.state = FMRB_LINK_GFX_VIDEO_STATE_PAUSED;
        }
        player_unlock();
        return FMRB_OK;

    case FMRB_LINK_GFX_VIDEO_ACTION_STOP:
        player_stop_and_wait();
        return FMRB_OK;

    case FMRB_LINK_GFX_VIDEO_ACTION_REWIND:
        if (!s_p.fp) return FMRB_ERR_INVALID_STATE;
        s_p.rewind_req = true;
        return FMRB_OK;

    default:
        return FMRB_ERR_INVALID_PARAM;
    }
}

void display_p4_video_get_status(display_p4_video_status_t *out)
{
    if (!out) return;
    player_lock();
    out->state          = s_p.state;
    out->canvas_id      = s_p.canvas_id;
    out->frames_shown   = s_p.frames_shown;
    out->frames_dropped = s_p.frames_dropped;
    player_unlock();
}

bool display_p4_video_is_active(void)
{
    return s_p.state == FMRB_LINK_GFX_VIDEO_STATE_PLAYING;
}

/* The copy runs on the display task and is timed there; the P4 build folds it
 * into a profile line. Nothing here keeps that statistic. */
void display_p4_video_note_copy_us(uint32_t us) { (void)us; }

void display_p4_video_canvas_gone(uint16_t canvas_id)
{
    if (s_p.fp && s_p.canvas_id == canvas_id) {
        FMRB_LOGI(TAG, "canvas %u deleted, stopping playback", canvas_id);
        player_stop_and_wait();
    }
}
