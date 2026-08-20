// Motion-JPEG playback (Modern / ESP32-P4). See display_p4_video.h.

#include "display_p4_video.h"

#include "fmrb_link_protocol.h"
#include "fmrb_log.h"
#include "fmrb_mem.h"
#include "fmrb_task_config.h"

#include "driver/jpeg_decode.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "p4_video";

// A frame larger than this is either not ours or a broken file. 320x240 at
// quality 90 sits around 25KB, so this is generous.
#define VIDEO_MAX_FRAME_BYTES (512 * 1024)
// Block size for reading the file. A whole frame is a few KB, so this turns
// one frame into a couple of reads instead of thousands.
#define VIDEO_IO_BUF_BYTES    (16 * 1024)
// Largest picture we will play: the whole framebuffer, rounded up to the
// decoder's 16-pixel grid.
#define VIDEO_MAX_W 448
#define VIDEO_MAX_H 256

#define ALIGN_UP16(v) (((v) + 15u) & ~15u)

// ============================================================
// Decoder wrapper
// ============================================================

static jpeg_decoder_handle_t s_decoder = NULL;
static SemaphoreHandle_t     s_decoder_lock = NULL;

static esp_err_t decoder_acquire(void)
{
    if (!s_decoder_lock) {
        s_decoder_lock = xSemaphoreCreateMutex();
        if (!s_decoder_lock) return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_decoder_lock, portMAX_DELAY);
    if (s_decoder) return ESP_OK;

    jpeg_decode_engine_cfg_t cfg = {};
    cfg.timeout_ms = 100;
    esp_err_t err = jpeg_new_decoder_engine(&cfg, &s_decoder);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "jpeg decoder engine create failed: %d", err);
        xSemaphoreGive(s_decoder_lock);
    }
    return err;
}

static void decoder_release(void)
{
    if (s_decoder_lock) xSemaphoreGive(s_decoder_lock);
}

bool display_p4_jpeg_is_jpeg(const uint8_t *data, size_t len)
{
    return data && len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

uint8_t *display_p4_jpeg_alloc_input(size_t size, size_t *out_alloc)
{
    jpeg_decode_memory_alloc_cfg_t cfg = {};
    cfg.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;
    size_t got = 0;
    uint8_t *buf = (uint8_t *)jpeg_alloc_decoder_mem(size, &cfg, &got);
    if (out_alloc) *out_alloc = got;
    return buf;
}

void display_p4_jpeg_free_input(uint8_t *buf)
{
    if (buf) free(buf);
}

void display_p4_jpeg_free_output(uint8_t *buf)
{
    if (buf) free(buf);
}

// Decode into a caller-provided buffer. Returns false if the picture does not
// fit or the hardware refuses the stream.
static bool jpeg_decode_into(const uint8_t *jpeg, size_t len,
                             uint8_t *out, size_t out_size,
                             uint16_t *out_w, uint16_t *out_h,
                             uint16_t *out_stride_px)
{
    jpeg_decode_picture_info_t info = {};
    esp_err_t err = jpeg_decoder_get_info(jpeg, (uint32_t)len, &info);
    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "jpeg header parse failed: %d", err);
        return false;
    }

    uint32_t stride = ALIGN_UP16(info.width);
    uint32_t rows   = ALIGN_UP16(info.height);
    if ((size_t)stride * rows * 2u > out_size) {
        FMRB_LOGE(TAG, "jpeg %ux%u does not fit the output buffer",
                  (unsigned)info.width, (unsigned)info.height);
        return false;
    }

    if (decoder_acquire() != ESP_OK) return false;

    jpeg_decode_cfg_t cfg = {};
    cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
    // For RGB565 a pixel is one 16-bit word, so this knob is really the byte
    // order: the driver documents BGR as "small endian" and RGB as "big
    // endian". The canvases hold little-endian RGB565 -- which is why the
    // remote-desktop encoder can eat canvas memory directly, with no order
    // knob at all -- so the decoder has to produce the small-endian one.
    // Choosing RGB here swaps the two bytes of every pixel, which reads as a
    // rotation of the colour channels (red->blue, green->red, blue->green),
    // not as the red/blue swap one would expect from an element-order bug.
    cfg.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
    cfg.conv_std  = JPEG_YUV_RGB_CONV_STD_BT601;

    uint32_t produced = 0;
    err = jpeg_decoder_process(s_decoder, &cfg, jpeg, (uint32_t)len,
                               out, (uint32_t)out_size, &produced);
    decoder_release();

    if (err != ESP_OK) {
        FMRB_LOGE(TAG, "jpeg decode failed: %d", err);
        return false;
    }

    if (out_w) *out_w = (uint16_t)info.width;
    if (out_h) *out_h = (uint16_t)info.height;
    if (out_stride_px) *out_stride_px = (uint16_t)stride;
    return true;
}

uint8_t *display_p4_jpeg_decode(const uint8_t *jpeg, size_t len,
                                uint16_t *out_w, uint16_t *out_h,
                                uint16_t *out_stride_px)
{
    jpeg_decode_picture_info_t info = {};
    if (jpeg_decoder_get_info(jpeg, (uint32_t)len, &info) != ESP_OK) {
        return NULL;
    }
    if (info.width == 0 || info.height == 0 ||
        info.width > 2048 || info.height > 2048) {
        FMRB_LOGE(TAG, "jpeg dimensions out of range: %ux%u",
                  (unsigned)info.width, (unsigned)info.height);
        return NULL;
    }

    size_t need = (size_t)ALIGN_UP16(info.width) * ALIGN_UP16(info.height) * 2u;
    jpeg_decode_memory_alloc_cfg_t mem_cfg = {};
    mem_cfg.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    size_t got = 0;
    uint8_t *out = (uint8_t *)jpeg_alloc_decoder_mem(need, &mem_cfg, &got);
    if (!out) {
        FMRB_LOGE(TAG, "jpeg output alloc failed (%u bytes)", (unsigned)need);
        return NULL;
    }

    if (!jpeg_decode_into(jpeg, len, out, got, out_w, out_h, out_stride_px)) {
        free(out);
        return NULL;
    }
    return out;
}

// ============================================================
// Frame slots
//
// The player decodes into whichever slot is free and marks it ready; the
// display task takes the ready one and returns it when the copy is done. Two
// slots are enough: the player can always work while the display task holds
// one, and a frame that is still ready when a newer one lands is simply
// dropped (the newer picture is the one worth showing).
// ============================================================

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

// ============================================================
// Player
// ============================================================

typedef struct {
    // Owned by the player task except where noted.
    FILE        *fp;
    char         path[128];
    uint16_t     canvas_id;
    int16_t      dst_x, dst_y;   // where the picture sits inside the canvas
    uint16_t     fps;
    uint8_t      flags;
    uint16_t     width, height, stride_px;

    uint8_t     *read_buf;       // JPEG bytes of the frame being decoded
    size_t       read_buf_size;

    // Block reader over the file. Going through fgetc means one locked stdio
    // call -- and, every time its small buffer runs dry, one trip into FATFS --
    // per byte, which measured out at about 40ms for a 9KB frame: far too slow
    // to hold 15 frames a second. Reading in blocks and scanning in RAM keeps
    // the SD access count proportional to the file, not to its bytes.
    uint8_t     *io_buf;
    size_t       io_len;         // bytes currently in io_buf
    size_t       io_pos;         // read cursor within io_buf

    frame_slot_t slots[VIDEO_SLOTS];
    uint32_t     next_seq;

    // Shared state (guarded by lock)
    volatile uint8_t state;      // fmrb_link_graphics_video_state_t
    volatile bool    stop_req;   // "close the file and go idle"
    volatile bool    rewind_req;
    uint32_t         frames_shown;
    uint32_t         frames_dropped;

    TaskHandle_t     task;
    SemaphoreHandle_t lock;
} video_player_t;

static video_player_t s_p = {};

static inline void player_lock(void)   { if (s_p.lock) xSemaphoreTake(s_p.lock, portMAX_DELAY); }
static inline void player_unlock(void) { if (s_p.lock) xSemaphoreGive(s_p.lock); }

// ---- file reading -----------------------------------------------------------

// Copy the next JPEG frame (SOI..EOI inclusive) into dst. Returns its length,
// or 0 at end of file / on a malformed stream.
//
// Scanning for FFD9 is safe for the streams we target: inside entropy-coded
// data every FF is stuffed as FF 00, and restart markers are FFD0..FFD7, so
// FFD9 only appears as the end of image. A file carrying JPEG thumbnails
// inside its metadata would break this; the converter documented for this
// feature does not produce them.
// One byte from the block reader, or -1 at end of file.
static inline int io_getc(void)
{
    if (s_p.io_pos >= s_p.io_len) {
        s_p.io_len = fread(s_p.io_buf, 1, VIDEO_IO_BUF_BYTES, s_p.fp);
        s_p.io_pos = 0;
        if (s_p.io_len == 0) return -1;
    }
    return s_p.io_buf[s_p.io_pos++];
}

// Go back to the first frame. The block reader holds bytes from wherever the
// file cursor used to be, so it has to be emptied along with the seek.
static void player_seek_start(void)
{
    fseek(s_p.fp, 0, SEEK_SET);
    s_p.io_len = 0;
    s_p.io_pos = 0;
}

static size_t read_next_frame(uint8_t *dst, size_t cap)
{
    int c;
    // Find SOI
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
        if (prev == 0xFF && c == 0xD9) {
            return len;
        }
        prev = c;
    }
    return 0;  // truncated tail
}

// ---- slots ------------------------------------------------------------------

static frame_slot_t *slot_take_for_write(void)
{
    frame_slot_t *pick = NULL;
    player_lock();
    for (int i = 0; i < VIDEO_SLOTS; i++) {
        if (s_p.slots[i].state == SLOT_FREE) { pick = &s_p.slots[i]; break; }
    }
    if (!pick) {
        // Nothing free: the display task holds one and the other is still
        // waiting to be shown. Overwrite the waiting one -- it is already old.
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
        // A second ready slot means we fell behind; it is stale now.
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

// ---- teardown ---------------------------------------------------------------

static void player_free_buffers(void)
{
    for (int i = 0; i < VIDEO_SLOTS; i++) {
        if (s_p.slots[i].buf) {
            free(s_p.slots[i].buf);
            s_p.slots[i].buf = NULL;
        }
        s_p.slots[i].size = 0;
        s_p.slots[i].state = SLOT_FREE;
    }
    if (s_p.read_buf) {
        free(s_p.read_buf);
        s_p.read_buf = NULL;
        s_p.read_buf_size = 0;
    }
    if (s_p.io_buf) {
        fmrb_sys_free(s_p.io_buf);
        s_p.io_buf = NULL;
    }
    s_p.io_len = 0;
    s_p.io_pos = 0;
}

// Close the file and drop every buffer. Runs on the player task, or on the
// caller's task while the player task is not running.
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

// ---- player task ------------------------------------------------------------

// Decode one frame from the file into a slot. Returns:
//   1 = a frame was published, 0 = end of file, -1 = fatal
static int player_step(bool decode)
{
    size_t len = read_next_frame(s_p.read_buf, s_p.read_buf_size);
    if (len == 0) return 0;
    if (!decode) return 1;

    frame_slot_t *slot = slot_take_for_write();
    if (!slot) return 1;  // cannot happen with 2 slots, but stay defensive

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
    int64_t next_due = esp_timer_get_time();

    while (1) {
        if (s_p.stop_req) break;

        if (s_p.state != FMRB_LINK_GFX_VIDEO_STATE_PLAYING) {
            // Paused or finished: idle cheaply and keep the clock from
            // accumulating a debt while we wait.
            vTaskDelay(pdMS_TO_TICKS(20));
            next_due = esp_timer_get_time();
            continue;
        }

        if (s_p.rewind_req) {
            s_p.rewind_req = false;
            player_seek_start();
            next_due = esp_timer_get_time();
        }

        int64_t now = esp_timer_get_time();
        if (now < next_due) {
            int64_t wait_ms = (next_due - now) / 1000;
            vTaskDelay(wait_ms > 0 ? pdMS_TO_TICKS(wait_ms) : 1);
            continue;
        }

        // Skipping one frame per turn only closes the gap while reading is
        // cheaper than the interval. Once the gap is several frames wide,
        // insisting on the old schedule means skipping the rest of the file
        // and showing nothing, so give up the debt and play on from here.
        if ((now - next_due) > 4 * interval_us) {
            next_due = now;
        }

        // More than one interval late: read the frame past without decoding
        // it. The clock is what the viewer notices, not the missing picture.
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
                next_due = esp_timer_get_time();
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

// ---- public API -------------------------------------------------------------

static void player_stop_and_wait(void)
{
    if (s_p.task) {
        s_p.stop_req = true;
        for (int i = 0; i < 200 && s_p.task; i++) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (s_p.task) {
            FMRB_LOGW(TAG, "player task did not stop in time");
        }
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
    if (!s_p.read_buf) {
        player_close_file();
        return FMRB_ERR_NO_MEMORY;
    }

    // The block buffer only feeds the scanner, so it has no alignment or DMA
    // requirement of its own.
    s_p.io_buf = (uint8_t *)fmrb_sys_malloc(VIDEO_IO_BUF_BYTES);
    s_p.io_len = 0;
    s_p.io_pos = 0;
    if (!s_p.io_buf) {
        player_close_file();
        return FMRB_ERR_NO_MEMORY;
    }

    // Learn the picture size from the first frame before allocating the
    // slots, then rewind so playback starts at frame 0.
    size_t first_len = read_next_frame(s_p.read_buf, s_p.read_buf_size);
    if (first_len == 0) {
        FMRB_LOGE(TAG, "%s holds no JPEG frame", path);
        player_close_file();
        return FMRB_ERR_INVALID_PARAM;
    }
    jpeg_decode_picture_info_t info = {};
    if (jpeg_decoder_get_info(s_p.read_buf, (uint32_t)first_len, &info) != ESP_OK ||
        info.width == 0 || info.height == 0 ||
        info.width > VIDEO_MAX_W || info.height > VIDEO_MAX_H) {
        FMRB_LOGE(TAG, "unsupported frame size %ux%u (max %ux%u)",
                  (unsigned)info.width, (unsigned)info.height,
                  VIDEO_MAX_W, VIDEO_MAX_H);
        player_close_file();
        return FMRB_ERR_INVALID_PARAM;
    }
    player_seek_start();

    s_p.width     = (uint16_t)info.width;
    s_p.height    = (uint16_t)info.height;
    s_p.stride_px = (uint16_t)ALIGN_UP16(info.width);

    size_t slot_size = (size_t)s_p.stride_px * ALIGN_UP16(info.height) * 2u;
    jpeg_decode_memory_alloc_cfg_t mem_cfg = {};
    mem_cfg.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    for (int i = 0; i < VIDEO_SLOTS; i++) {
        size_t got = 0;
        s_p.slots[i].buf = (uint8_t *)jpeg_alloc_decoder_mem(slot_size, &mem_cfg, &got);
        s_p.slots[i].size = got;
        s_p.slots[i].state = SLOT_FREE;
        if (!s_p.slots[i].buf) {
            FMRB_LOGE(TAG, "frame slot alloc failed (%u bytes)", (unsigned)slot_size);
            player_close_file();
            return FMRB_ERR_NO_MEMORY;
        }
    }

    player_lock();
    s_p.state = FMRB_LINK_GFX_VIDEO_STATE_PAUSED;
    player_unlock();

    BaseType_t ok = xTaskCreatePinnedToCore(player_task, "p4_video",
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
        if (s_p.state == FMRB_LINK_GFX_VIDEO_STATE_FINISHED) {
            s_p.rewind_req = true;
        }
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

void display_p4_video_canvas_gone(uint16_t canvas_id)
{
    if (s_p.fp && s_p.canvas_id == canvas_id) {
        FMRB_LOGI(TAG, "canvas %u deleted, stopping playback", canvas_id);
        player_stop_and_wait();
    }
}
