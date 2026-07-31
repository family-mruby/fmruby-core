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
                          int bg_color, bool has_bg);
int fmrb_mp_gfx_present(int canvas_id, int x, int y, bool explicit_pos);

/** Log one line at the given level ('D', 'I', 'W', 'E'). */
void fmrb_mp_bridge_log(char level, const char *msg);

#ifdef __cplusplus
}
#endif
