/**
 * @file fmrb_mp_bridge.h
 * @brief Plain-C bridge between the _fmrb MicroPython module and fmruby-core
 *
 * fmrb_module.c is fed to the C preprocessor by the qstr extractor during
 * "rake micropython:gen", using the host compiler with no ESP-IDF headers in
 * sight. So it may not include anything from the firmware, and everything that
 * does lives behind this header, implemented in fmrb_bridge.c.
 *
 * Keep the declarations here free of firmware types: fixed-width integers and
 * plain pointers only.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Copies of FMRB_MAX_MSG_PAYLOAD_SIZE and two message types from fmrb_msg.h,
 * which the module side cannot include. fmrb_bridge.c static-asserts them
 * against the real definitions, so drift is a build error rather than a
 * truncated message or a command routed to the wrong handler.
 */
#define FMRB_MP_MSG_BUF_SIZE (176)
#define FMRB_MP_MSG_TYPE_APP_CONTROL (0)
#define FMRB_MP_MSG_TYPE_HID_EVENT (3)

/** Window and canvas facts the app framework needs at startup. */
typedef struct {
    const char *name;        // app name, valid until the app exits
    int32_t canvas_id;       // -1 when headless
    int32_t bg_canvas_id;    // -1 when absent
    uint16_t window_width;
    uint16_t window_height;
    uint16_t pos_x;
    uint16_t pos_y;
    bool fullscreen;
    bool rounded_corners;
    bool headless;
    bool is_esp32;           // false = linux simulation
} fmrb_mp_app_info_t;

/**
 * @brief Set up this app's window: creates the canvas unless headless
 * @return 0 on success, negative on failure
 */
int fmrb_mp_bridge_app_init(fmrb_mp_app_info_t *out);

/** Release the canvas and message queue. Safe to call twice. */
void fmrb_mp_bridge_app_cleanup(void);

/** True once the kernel has asked this app to stop. */
bool fmrb_mp_bridge_should_exit(void);

/**
 * @brief The system theme's nine colours (RGB332)
 * @param out desktop_bg, menu_bg, window_bg, text, text_light, highlight,
 *            border, button, dir_color -- the field order of fmrb_theme_t
 *
 * Readable before the app window exists, because the prelude turns them into
 * FmrbConst.THEME_* at import time, the way Ruby gets them as constants.
 */
void fmrb_mp_bridge_theme(uint8_t out[9]);

/** Milliseconds since boot, for the spin loop's own timekeeping. */
uint32_t fmrb_mp_bridge_now_ms(void);

/**
 * @brief Wait for one message addressed to this app
 * @param buf Destination for the payload
 * @param buf_size Size of buf
 * @param out_type Message type (FMRB_MSG_TYPE_*)
 * @param timeout_ms How long to wait
 * @return Payload length, or -1 when nothing arrived
 */
int fmrb_mp_bridge_recv(uint8_t *buf, size_t buf_size, int *out_type,
                        uint32_t timeout_ms);

/**
 * @brief Latch a stop request carried by this message
 *
 * Runtimes that drain the queue themselves must hand every message here, or a
 * kernel stop is swallowed along with the rest and only a forced kill ends the
 * app.
 */
void fmrb_mp_bridge_note_control(int msg_type, const uint8_t *payload, uint32_t size);

/** Send a message to another app. Returns true on success. */
bool fmrb_mp_bridge_send(int dest_pid, int msg_type, const uint8_t *data, uint32_t size);

/** Update this app's window position (the canvas follows on the next present). */
void fmrb_mp_bridge_set_window_pos(int32_t x, int32_t y);

/** True when this app was started from a file rather than built in. */
bool fmrb_mp_bridge_is_file_app(void);

/**
 * Drawing. Every call is one command on the host queue, in the same shape the
 * mruby and Spinel bindings use. Returns 0 on success, negative on failure.
 */
int fmrb_mp_gfx_clear(int canvas_id, int color);
int fmrb_mp_gfx_set_pixel(int canvas_id, int x, int y, int color);
int fmrb_mp_gfx_draw_line(int canvas_id, int x0, int y0, int x1, int y1, int color);
int fmrb_mp_gfx_rect(int canvas_id, int x, int y, int w, int h, int color, bool filled);
int fmrb_mp_gfx_circle(int canvas_id, int x, int y, int r, int color, bool filled);
int fmrb_mp_gfx_round_rect(int canvas_id, int x, int y, int w, int h, int r, int color,
                           bool filled);
int fmrb_mp_gfx_ellipse(int canvas_id, int x, int y, int rx, int ry, int color, bool filled);
int fmrb_mp_gfx_triangle(int canvas_id, int x0, int y0, int x1, int y1, int x2, int y2,
                         int color, bool filled);
int fmrb_mp_gfx_draw_text(int canvas_id, int x, int y, const char *text, int color,
                          int bg_color, bool has_bg, bool mixed);
int fmrb_mp_gfx_present(int canvas_id, int x, int y, bool explicit_pos);

/** Font family: 0 = the built-in 6x8 one, 1 = the Japanese one. */
int fmrb_mp_gfx_set_font(int canvas_id, int family, int size);
/** Scale factor applied on top of the font, 1 to 4. */
int fmrb_mp_gfx_set_text_size(int canvas_id, int size);

/**
 * Images living on the graphics side.
 *
 * The pixels never pass through Python: an app names a file, the firmware
 * copies it across if the copy there differs, and the graphics side decodes it.
 */

/** Copy src to dest on the graphics side, unless the copy there matches. */
bool fmrb_mp_gfx_sync_file(const char *src, const char *dest);

/**
 * @brief Decode a file on the graphics side into an image
 * @return 0 on success, negative on failure (missing file, bad format)
 */
int fmrb_mp_gfx_create_image(int canvas_id, const char *path, uint16_t *out_id,
                             uint16_t *out_width, uint16_t *out_height);

/** Scale is fixed point: 256 = 1.0. scale_y_fp8 of 0 means "same as x". */
int fmrb_mp_gfx_draw_image(int canvas_id, int image_id, int x, int y,
                           int scale_x_fp8, int scale_y_fp8);
int fmrb_mp_gfx_delete_image(int canvas_id, int image_id);

/** Stamp a rectangle of a sprite image onto the canvas, without an instance. */
int fmrb_mp_gfx_draw_tile(int canvas_id, int image_id, int src_x, int src_y, int w,
                          int h, int dst_x, int dst_y);

/**
 * Sprites. An image holds pixels, an instance places one or more images on the
 * canvas; moving an instance costs one command and the compositing happens on
 * the graphics side.
 */

/** Copy of FMRB_SPRITE_MAX_FRAMES (fmrb_link_protocol.h), static-asserted. */
#define FMRB_MP_SPRITE_MAX_FRAMES (8)

/** @return image id (> 0), or 0 on failure */
int fmrb_mp_gfx_create_sprite_image(int canvas_id, int width, int height,
                                    int transparent_color, bool use_transparent);
int fmrb_mp_gfx_load_sprite_image_bmp(int canvas_id, int image_id, const char *path);
int fmrb_mp_gfx_delete_sprite_image(int canvas_id, int image_id);
/** Redirect drawing into a sprite image; 0 puts it back on the canvas. */
int fmrb_mp_gfx_set_sprite_target(int canvas_id, int image_id);

/** @return instance id (> 0), or 0 on failure */
int fmrb_mp_gfx_create_sprite_instance(int canvas_id, const uint16_t *image_ids,
                                       int frame_count, int x, int y, int z_order);
int fmrb_mp_gfx_sprite_move(int canvas_id, int instance_id, int x, int y);
int fmrb_mp_gfx_sprite_visible(int canvas_id, int instance_id, bool visible);
int fmrb_mp_gfx_sprite_frame(int canvas_id, int instance_id, int frame_index);
int fmrb_mp_gfx_delete_sprite_instance(int canvas_id, int instance_id);
int fmrb_mp_gfx_delete_all_sprites(int canvas_id);

/** UI language from system_conf.toml ("ja" / "en"). Never NULL. */
const char *fmrb_mp_bridge_language(void);

/**
 * @brief Send one APU note on / off
 *
 * Notes are the only audio message sent in a stream, so this takes the values
 * rather than a message: nothing is allocated on either side of the call, in
 * any language. Everything else about audio is an ordinary send_message.
 *
 * @return true when the message was queued
 */
bool fmrb_mp_bridge_audio_note(bool on, int channel, int freq, int volume,
                               int duty, int sweep);

/** Log one line at the given level ('D', 'I', 'W', 'E'). */
void fmrb_mp_bridge_log(char level, const char *msg);

/**
 * Files. Guests get no open()/write(): they read whole files, either as data
 * (_fmrb.read_file) or as a module the importer compiles.
 */

/** What lives at this path. Matches mp_import_stat_t's three cases. */
typedef enum {
    FMRB_MP_PATH_NONE = 0,
    FMRB_MP_PATH_FILE = 1,
    FMRB_MP_PATH_DIR = 2,
} fmrb_mp_path_kind_t;

fmrb_mp_path_kind_t fmrb_mp_bridge_path_kind(const char *path);

/**
 * @brief Size of a file in bytes
 * @return 0 on success, negative when the path is missing or is a directory
 */
int fmrb_mp_bridge_file_size(const char *path, uint32_t *out_size);

/**
 * @brief Read a whole file into a caller-provided buffer
 * @param size Bytes to read; the caller sizes the buffer from file_size
 * @return 0 when exactly size bytes were read, negative otherwise
 */
int fmrb_mp_bridge_file_read(const char *path, uint8_t *buf, uint32_t size);

/**
 * @brief Directory this app was started from, for sys.path
 * @param out Destination buffer
 * @param cap Size of out
 * @return true when out holds a directory path ("/app/game" and the like)
 */
bool fmrb_mp_bridge_app_dir(char *out, size_t cap);

#ifdef __cplusplus
}
#endif
