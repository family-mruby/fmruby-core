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
 *
 * Returns the payload as a binary-safe string (for Spinel's `:binstr` FFI
 * return: the byte count is published in sp_ffi_bin_len so embedded NUL bytes
 * survive). type/src_pid are written to the out-params. On timeout the return
 * is an empty string with *type = -1.
 *
 * @param timeout_ms max ms to wait for a message
 * @param[out] type     message type (fmrb_msg_type_t), or -1 on timeout
 * @param[out] src_pid  source proc id, or -1 on timeout
 * @return pointer to the payload bytes (valid until the next call); length is
 *         in sp_ffi_bin_len.
 */
const char *fmrb_spx_recv_message(int timeout_ms, int *type, int *src_pid);

/**
 * @brief Send a raw message (blocking up to 100 ms for queue space).
 * @return 1 on success, 0 if not delivered, negative on error.
 */
int fmrb_spx_send_raw(int dst_pid, int type, const uint8_t *data, int len);

/** @brief Non-blocking send (drops if the destination queue is full). */
int fmrb_spx_try_send_raw(int dst_pid, int type, const uint8_t *data, int len);

/**
 * @brief Snapshot the active window list as packed records.
 *
 * Returned as :binstr (a real String the Ruby side reads with getbyte; an
 * ffi_buffer would return a :ptr, which has no getbyte). The byte length is
 * count * FMRB_SPX_WIN_RECORD_SIZE, published in sp_net_bin_len. On error the
 * return is an empty string.
 * @return pointer to N * FMRB_SPX_WIN_RECORD_SIZE bytes (length in sp_net_bin_len).
 */
const char *fmrb_spx_windows_snapshot(void);

int fmrb_spx_set_hid_target(int pid);        /**< 0 ok, negative error */
int fmrb_spx_set_focused_window(int win_id); /**< 0 ok, negative error */
int fmrb_spx_bring_to_front(int pid);        /**< 1 ok, 0 no, negative error */
int fmrb_spx_update_window_pos(int pid, int x, int y);   /**< 1/0/neg */
int fmrb_spx_update_window_size(int pid, int w, int h);  /**< 1/0/neg */
/** Switch a running app between windowed and fullscreen (no respawn). 1/0/neg */
int fmrb_spx_set_app_fullscreen(int pid, int on, int w, int h);
int fmrb_spx_suspend_app(int pid);           /**< 0 ok, negative error */
int fmrb_spx_resume_app(int pid);            /**< 0 ok, negative error */
int fmrb_spx_reap_app(int pid);              /**< 0 ok, negative error */
/** Note that this app is ending because the kernel asked it to
    (fmrb_app.h expected_stop). 0 ok, negative when the pid has no context. */
int fmrb_spx_mark_expected_stop(int pid);

/**
 * @brief Request spawning an app by name.
 * @return the new PID (>= 0) or negative on failure.
 */
int fmrb_spx_spawn_app_req(const char *name, int len);

/**
 * @brief Snapshot one app's info as a packed record. Mirrors the mruby
 *        _get_app_info hash. Fixed little-endian layout (read on the Ruby side
 *        by offset):
 *
 *   off  size  field
 *   0    1     valid   (1 if the pid has a context, else 0)
 *   1    1     fullscreen (bool)
 *   2    1     vm_type    1=mruby 2=lua 3=basic 4=native
 *   3    1     load_mode  (fmrb_load_mode_t)
 *   4    32    name       (NUL-padded)
 *   36   128   path       (NUL-padded; only for FILE load mode, else zeroed)
 *   164  1     fullscreen_switchable (bool; app survives Ctrl+Tab park/unpark)
 *   165  1     headless   (bool; default_window_mode = "background", no canvas)
 *
 * Total 166 bytes, returned as :binstr (length in sp_net_bin_len). When the pid
 * has no context the return is an empty string and the Ruby side returns nil.
 */
#define FMRB_SPX_APP_INFO_RECORD_SIZE 167
const char *fmrb_spx_app_info_snapshot(int pid);

/**
 * @brief The last app error as two NUL-padded fields (:binstr, length in
 *        sp_net_bin_len): name at offset 0 (width 64), message at offset 64
 *        (width 112). Empty string when there is no error (Ruby returns nil).
 */
#define FMRB_SPX_LAST_ERROR_RECORD_SIZE 176
const char *fmrb_spx_last_error(void);

/** @brief Set the status-LED error pattern (FmrbConst::LED_ERR_*). Returns 0. */
int fmrb_spx_set_error_led(int level);

/** @brief Signal the kernel is ready (boot handshake). Returns 0. */
int fmrb_spx_set_ready(void);

/** @brief Check the host protocol version. 1 ok / 0 fail / negative on error. */
int fmrb_spx_check_protocol_version(int timeout_ms);

/** @brief Check the graphics-audio firmware version. 1 ok / 0 fail / neg err. */
int fmrb_spx_check_ga_version(int timeout_ms);

/**
 * @brief Send the current wall-clock time (and TZ) to the host.
 * @return 1 on success, 0 if not delivered, negative on error.
 */
int fmrb_spx_sync_time_to_host(void);

/** @brief How many files system_conf.toml asks to sync. */
int fmrb_spx_sync_file_count(void);

/**
 * @brief One sync entry as a :binstr record: src (128, NUL-pad) then dest (128).
 * @return Empty string when the index has no entry.
 */
const char *fmrb_spx_sync_file_entry(int index);

/** @brief Sync one file to the host. 1 on success (or already current), else 0. */
int fmrb_spx_sync_file(const char *src, int src_len, const char *dest, int dest_len);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_SPX_H */
