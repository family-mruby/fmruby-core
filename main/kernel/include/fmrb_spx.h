/**
 * @file fmrb_spx.h
 * @brief Spinel FFI shim: an mrb-free C ABI for the (future) Spinel-compiled
 *        kernel VM.
 *
 * Spinel-generated C cannot take an mrb_state and cannot receive C structs
 * through its FFI. This layer exposes the kernel's lower-level operations as
 * plain C functions that Spinel's `ffi_func` can call, sharing the same
 * underlying fmrb_* implementation as the mruby binding
 * (lib/add/picoruby-fmrb-kernel/ports/esp32/kernel.c).
 *
 * Design (see doc/spinel_aot/phase1.md T1-5):
 * - Return convention: `int` where negative is an error (fmrb_err_t negated or
 *   a shim-local negative code) and >= 0 is a result/length. Booleans return
 *   1 (true) / 0 (false) / negative (error). Functions with no result return
 *   0 on success.
 * - Structured data crosses the boundary as a **fixed-layout little-endian
 *   byte buffer**, never a C struct. The Ruby side parses it with getbyte, as
 *   the kernel code already does for HID/msgpack payloads.
 * - Do NOT box the bytes into a symbol-keyed Hash on a hot path: that reintro-
 *   duces poly and loses Spinel's type specialization (Phase 0 finding). Read
 *   the buffer directly with getbyte where performance matters.
 */
#ifndef FMRB_SPX_H
#define FMRB_SPX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Negative shim error codes (distinct from negated fmrb_err_t values). */
#define FMRB_SPX_ERR (-1)          /**< generic failure */
#define FMRB_SPX_ERR_RANGE (-2)    /**< argument out of range */
#define FMRB_SPX_ERR_CAP (-3)      /**< caller buffer too small */

/**
 * @brief Packed window record layout for fmrb_spx_windows_snapshot().
 *
 * Fixed 48-byte little-endian record; the Ruby side reads fields by offset
 * with getbyte. Mirrors fmrb_window_info_t (components/fmrb_common/fmrb_app.h).
 *
 *   off  size  field
 *   0    1     pid
 *   1    1     z_order
 *   2    1     flags   bit0=fullscreen, bit1=resizable
 *   3    1     (reserved, 0)
 *   4    2     x            (u16 LE)
 *   6    2     y            (u16 LE)
 *   8    2     width        (u16 LE)
 *   10   2     height       (u16 LE)
 *   12   2     min_width    (u16 LE)
 *   14   2     min_height   (u16 LE)
 *   16   32    app_name     (NUL-padded, not necessarily NUL-terminated)
 */
#define FMRB_SPX_WIN_RECORD_SIZE 48
#define FMRB_SPX_WIN_FLAG_FULLSCREEN 0x01
#define FMRB_SPX_WIN_FLAG_RESIZABLE  0x02

/** @brief Monotonic milliseconds since boot (replaces Machine.board_millis). */
uint32_t fmrb_spx_board_millis(void);

/**
 * @brief Write a log line at the given level.
 * @param level 0=debug 1=info 2=warn 3=error (others treated as info)
 * @param msg   UTF-8 text (need not be NUL-terminated)
 * @param len   byte length of @p msg
 */
void fmrb_spx_log_write(int level, const char *msg, int len);

/**
 * @brief Poll one message for the kernel proc (blocking up to @p timeout_ms).
 * @param buf        destination for the payload bytes
 * @param cap        capacity of @p buf
 * @param timeout_ms max ms to wait for a message
 * @param[out] type     message type (fmrb_msg_type_t)
 * @param[out] src_pid  source proc id
 * @return payload length (>= 0) on receipt; 0 with *type = -1 on timeout;
 *         negative on error or when the payload exceeds @p cap
 *         (FMRB_SPX_ERR_CAP).
 */
int fmrb_spx_recv_message(uint8_t *buf, int cap, int timeout_ms,
                          int *type, int *src_pid);

/**
 * @brief Send a raw message (blocking up to 100 ms for queue space).
 * @return 1 on success, 0 if not delivered, negative on error.
 */
int fmrb_spx_send_raw(int dst_pid, int type, const uint8_t *data, int len);

/** @brief Non-blocking send (drops if the destination queue is full). */
int fmrb_spx_try_send_raw(int dst_pid, int type, const uint8_t *data, int len);

/**
 * @brief Snapshot the active window list as packed records.
 * @param buf destination for N * FMRB_SPX_WIN_RECORD_SIZE bytes
 * @param cap capacity of @p buf
 * @return record count (>= 0), or FMRB_SPX_ERR_CAP if @p cap is too small.
 */
int fmrb_spx_windows_snapshot(uint8_t *buf, int cap);

int fmrb_spx_set_hid_target(int pid);        /**< 0 ok, negative error */
int fmrb_spx_set_focused_window(int win_id); /**< 0 ok, negative error */
int fmrb_spx_bring_to_front(int pid);        /**< 1 ok, 0 no, negative error */
int fmrb_spx_update_window_pos(int pid, int x, int y);   /**< 1/0/neg */
int fmrb_spx_update_window_size(int pid, int w, int h);  /**< 1/0/neg */
int fmrb_spx_suspend_app(int pid);           /**< 0 ok, negative error */
int fmrb_spx_resume_app(int pid);            /**< 0 ok, negative error */
int fmrb_spx_reap_app(int pid);              /**< 0 ok, negative error */

/**
 * @brief Request spawning an app by name.
 * @return the new PID (>= 0) or negative on failure.
 */
int fmrb_spx_spawn_app_req(const char *name, int len);

/**
 * @brief Snapshot one app's info as a packed record (layout documented in the
 *        implementation; Phase 2 will finalize it). Returns bytes written or
 *        negative.
 */
int fmrb_spx_app_info_snapshot(int pid, uint8_t *buf, int cap);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_SPX_H */
