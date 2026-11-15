/**
 * @file fmrb_spx_app.h
 * @brief Spinel FFI shim for FmrbApp: an mrb-free C ABI wrapping the same
 *        app-lifecycle / system-info operations the mruby FmrbApp binding uses
 *        (lib/gems/picoruby-fmrb-app/ports/esp32/app.c).
 *
 * See fmrb_spx_gfx.h for the shared return convention. Structured results cross
 * the boundary as fixed-layout little-endian byte buffers returned via :binstr
 * (byte length in sp_ffi_bin_len); an empty String means "nil".
 *
 * The message poll (fmrb_spx_app_recv_message) replaces FmrbApp#_spin: the
 * mruby _spin dispatched HID events by calling on_event() through mrb_funcall,
 * which Spinel cannot do (no C->Ruby call). The Spinel base class instead polls
 * one raw message here and parses/dispatches it in Ruby (see phase4.md T4-2).
 */
#ifndef FMRB_SPX_APP_H
#define FMRB_SPX_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FMRB_SPX_ERR
#define FMRB_SPX_ERR (-1)
#define FMRB_SPX_ERR_RANGE (-2)
#define FMRB_SPX_ERR_CAP (-3)
#endif

/* ---- instance lifecycle ------------------------------------------------- */

/**
 * @brief Initialize the app from its task context and create its canvas(es).
 *
 * Reproduces FmrbApp#_init: creates the main window canvas (and, for the
 * desktop, the background canvas) and stores their ids in the task context.
 * Returns a fixed 52-byte :binstr record the Ruby base class reads to set its
 * instance variables:
 *
 *   off  size  field
 *   0    32    name        (NUL-padded)
 *   32   1     fullscreen  (bool)
 *   33   1     rounded_corners (bool)
 *   34   1     platform    (0=linux, 1=esp32)
 *   35   1     headless    (bool)
 *   36   2     window_width  (u16 LE)
 *   38   2     window_height (u16 LE)
 *   40   2     pos_x         (u16 LE)
 *   42   2     pos_y         (u16 LE)
 *   44   1     has_canvas    (1 if a main canvas was created)
 *   45   1     has_bg        (1 if a background canvas was created)
 *   46   2     canvas_id     (u16 LE; valid only if has_canvas)
 *   48   2     bg_canvas_id  (u16 LE; valid only if has_bg)
 *   50   1     resizable   (bool: the kernel resizes it from its corner)
 *   51   1     frameless   (bool: window_frame = "none")
 */
#define FMRB_SPX_APP_INIT_RECORD_SIZE 52
const char *fmrb_spx_app_init(void);

/**
 * @brief Poll one message for this app (blocking up to @p timeout_ms).
 *
 * Returns the payload as a binary-safe :binstr (length in sp_ffi_bin_len) and
 * writes the message type / source pid to the out-params. On timeout the String
 * is empty and *type = -1.
 */
const char *fmrb_spx_app_recv_message(int timeout_ms, int *type, int *src_pid);

/** Release this app's canvas and message queue (FmrbApp#_cleanup). Returns 0. */
int fmrb_spx_app_cleanup(void);
/** Note that this app is ending because it meant to (fmrb_app.h
    expected_stop). Called from the Spinel base's FmrbApp#stop. Returns 0. */
int fmrb_spx_app_mark_expected_stop(void);

/** Send a message to another task. 1 on success, 0 if undelivered, neg on err. */
int fmrb_spx_app_send_message(int dest_pid, int msg_type, const char *data, int len);

/** which: 0 = pos_x, 1 = pos_y. Updates the context and returns 0 / neg. */
int fmrb_spx_app_set_window_param(int which, int value);
/** @brief Declare this app's hit shape (fmrb_app_set_hit_shape); len 0 withdraws. 1 ok, 0 refused. */
int fmrb_spx_app_set_hit_shape(const char *data, int len);
/** @brief Move / show / hide a hit-marked sprite's entries (fmrb_app_hit_shape_place_sprite). */
int fmrb_spx_app_hit_sprite_place(int sprite_id, int x, int y, int visible);

/** 1 if this app was loaded from a file (FMRB_LOAD_MODE_FILE), else 0. */
int fmrb_spx_app_is_file_app(void);

/** Create an extra canvas (FmrbApp#_create_canvas). Returns canvas id or neg. */
int fmrb_spx_app_create_canvas(int w, int h, int z_offset, int use_transparent, int transparent_color);

/** Delete an extra canvas owned by this app. Returns 0 / neg. */
int fmrb_spx_app_delete_canvas(int canvas_id);

/* ---- class: process / memory info --------------------------------------- */

/**
 * @brief Process list snapshot (FmrbApp.ps). :binstr of N fixed 60-byte
 *        records (length = N * 60):
 *   0  1   id        4  4  gen         12 4  mem_total   24 4  mem_frag (i32)
 *   1  1   state     8  4  stack_water 16 4  mem_used    28 32 name (NUL-padded)
 *   2  1   type                        20 4  mem_free
 *   3  1   vm_type
 * (u32 fields are little-endian.)
 */
#define FMRB_SPX_APP_PS_RECORD_SIZE 64
const char *fmrb_spx_app_ps(void);

/**
 * @brief System heap info (FmrbApp.heap_info). :binstr of six u32 LE:
 *        free, total, min_free, largest_block, iram_free, iram_total.
 */
#define FMRB_SPX_APP_HEAP_RECORD_SIZE 24
const char *fmrb_spx_app_heap_info(void);

/**
 * @brief System pool info (FmrbApp.sys_pool_info). :binstr of five u32 LE:
 *        total, used, free, used_blocks, free_blocks.
 */
#define FMRB_SPX_APP_SYSPOOL_RECORD_SIZE 20
const char *fmrb_spx_app_sys_pool_info(void);

/** @brief GFX counters (FmrbApp.gfx_stats). :binstr of two u32 LE: cmds, presents. */
#define FMRB_SPX_APP_GFXSTATS_RECORD_SIZE 8
const char *fmrb_spx_app_gfx_stats(void);

/**
 * @brief Last app error (FmrbApp._get_last_error). :binstr with name at offset
 *        0 (width 64) and message at offset 64 (width 112); empty String when
 *        there is no error (Ruby returns nil).
 */
#define FMRB_SPX_APP_LASTERR_RECORD_SIZE 176
const char *fmrb_spx_app_last_error(void);

/* ---- class: configuration / clock --------------------------------------- */

/**
 * @brief Read a system_conf.toml section (FmrbApp.config). Returns an empty
 *        String (Ruby nil) when the section is absent, otherwise a packed
 *        :binstr the Ruby side parses into an Array of String=>String Hashes:
 *
 *        [table_count u8]
 *        repeat table_count times:
 *          [kv_count u8]
 *          repeat kv_count times:
 *            [key_len u8][key bytes][val_len u16 LE][val bytes]
 */
const char *fmrb_spx_app_config(const char *section, int len);

/**
 * @brief UI language code from system_conf ("en" / "ja"), as a :binstr.
 *
 * The generated FmrbConst is a compile-time table, so a Spinel program cannot
 * read a setting the user can change; this is the run-time source. The mruby
 * side gets the same value through FmrbConst::LANGUAGE.
 */
const char *fmrb_spx_app_language(void);

/**
 * @brief The machine's clipboard (fmrb_clipboard.h, L12a): its length in
 *        bytes and a piece of it (the generation and setting it are the app
 *        boundary's, fmrb_bnd_app_*).
 *
 * chunk returns, as a :binstr, up to FMRB_SPX_APP_CLIP_CHUNK bytes from byte
 * off, never past limit (> 0) cut before a character that would not fit
 * whole; empty past the end. The Ruby side reads it in pieces and checks the
 * generation around them.
 */
#define FMRB_SPX_APP_CLIP_CHUNK 4096
int fmrb_spx_app_clipboard_length(void);
const char *fmrb_spx_app_clipboard_chunk(int off, int limit);
int fmrb_spx_app_set_text_cursor(int x, int y, int h);

/** The record of set_wallclock: six u16 LE (year, month, day, hour, minute, second). */
#define FMRB_SPX_APP_WALLCLOCK_RECORD_SIZE 12

/**
 * @brief Set the wall clock from LOCAL time fields (FmrbSystem.set_wallclock) and
 *        return the equivalent UTC fields as a 12-byte :binstr (same layout as
 *        wallclock), or an empty String on failure.
 */
const char *fmrb_spx_app_set_wallclock(int year, int month, int day,
                                       int hour, int minute, int second);

/* ---- class: cursor / power ---------------------------------------------- */

int fmrb_spx_app_enable_cursor(void);            /**< returns 0 */
int fmrb_spx_app_set_cursor_visible(int visible);/**< returns 0 */
int fmrb_spx_app_reboot(void);                   /**< does not return */
int fmrb_spx_app_power_actions(void);            /**< FmrbSystem.power_actions as fmrb_sys_power_t bits */
int fmrb_spx_app_power(int action);              /**< FmrbSystem.power: one bit; 1 done, 0 not offered/failed */
/** FmrbApp.battery_level / battery_charging?: -1 without a battery value,
 *  else the percent (0-100) with 0x100 set while charging. */
int fmrb_spx_app_battery(void);
int fmrb_spx_app_ble_start(void);                /**< manual BLE start; 1 on success */
int fmrb_spx_app_ble_state(void);                /**< FmrbSystem.ble_state (0 = off) */
int fmrb_spx_app_ble_settings_window(int seconds); /**< open/close the BLE settings window; seconds left */
int fmrb_spx_app_ble_settings_left(void);        /**< seconds left in the settings window (0 = closed) */
int fmrb_spx_app_audio_muted(void);              /**< FmrbApp.audio_muted? (1/0) */
int fmrb_spx_app_path_read_only(const char *path, int len); /**< FmrbApp.path_read_only? (1/0) */
/** FmrbApp.app_meta(path): the merged .app.toml and "#---fmrb" comment,
 *  packed (:binstr, "" when the file is not an app; fmrb_app_meta.h). */
const char *fmrb_spx_app_meta(const char *path, int len);
/** @brief FmrbApp.move(pid, to): ask the kernel to move an app over Asterism
 *  (the caller hears "checkpoint_result"). :binstr, "" when asked, else why not. */
const char *fmrb_spx_app_move(int pid, const char *to, int len, int hold_ms);
/** @brief FmrbApp.checkpoint(pid, restart:): ask for an app's checkpoint image.
 *  :binstr, "" when asked, else why not. */
const char *fmrb_spx_app_checkpoint(int pid, int restart);
int fmrb_spx_app_set_audio_mute(int on);         /**< FmrbSystem.set_audio_mute */
int fmrb_spx_app_audio_volume(void);             /**< FmrbApp.audio_volume (0-10) */
int fmrb_spx_app_set_audio_volume(int step);     /**< FmrbSystem.set_audio_volume */
int fmrb_spx_app_preview_audio_volume(int step); /**< FmrbSystem.preview_audio_volume */
int fmrb_spx_app_audio_conf_saved(void);         /**< FmrbSystem.audio_conf_saved */
int fmrb_spx_app_wifi_connected(void);           /**< FmrbApp.wifi_connected? (1/0) */
int fmrb_spx_app_proc_generation(void);          /**< FmrbApp.ps_gen */
/** @brief FmrbConst.bt_mac: "AA:BB:CC:DD:EE:FF" :binstr, or "-" until known. */
const char *fmrb_spx_app_bt_mac(void);

/* ---- class: network / usb ----------------------------------------------- */

/**
 * @brief Network state (FmrbApp.wifi_info). Empty String (Ruby nil) on targets
 *        without networking (Retro). Otherwise an 82-byte :binstr:
 *   0   1   connected (bool)
 *   1   16  ip        (NUL-padded)
 *   17  33  ssid      (NUL-padded)
 *   50  32  hostname  (NUL-padded)
 */
#define FMRB_SPX_APP_WIFI_RECORD_SIZE 82
const char *fmrb_spx_app_wifi_info(void);

/**
 * @brief The Wi-Fi page's text (FmrbSystem.wifi_networks, fmrb_platform_sys.h
 *        wifi_list) as a :binstr; empty where Family mruby does not choose
 *        the network. A text longer than FMRB_SPX_APP_WIFI_LIST_MAX is cut
 *        after its last whole line. scan != 0 also asks for a scan.
 */
#define FMRB_SPX_APP_WIFI_LIST_MAX 2048
const char *fmrb_spx_app_wifi_networks(int scan);

/**
 * @brief Clear a cache directory on the graphics board (FmrbApp._clear_cache).
 *        9-byte :binstr: ok (u8), deleted (u32 LE), status (i32 LE). Always
 *        returns a record (never nil).
 */
#define FMRB_SPX_APP_CLEARCACHE_RECORD_SIZE 9
const char *fmrb_spx_app_clear_cache(const char *path, int len);

/**
 * @brief Connected USB HID devices (FmrbApp.usb_devices). :binstr of N fixed
 *        10-byte records (length = N * 10):
 *   0  1  type (fmrb_usb_dev_type_t: 1=KBD 2=MOUSE 3=GAMEPAD 4=OTHER)
 *   1  1  layout_valid
 *   2  2  vid (u16 LE)   4  2  pid (u16 LE)   6  1  addr   7  1  slot
 *   8  2  report_len (u16 LE)
 */
#define FMRB_SPX_APP_USBDEV_RECORD_SIZE 10
const char *fmrb_spx_app_usb_devices(void);

int fmrb_spx_app_hid_raw_subscribe(int slot);   /**< 1 ok / 0 fail */
int fmrb_spx_app_hid_raw_unsubscribe(int slot); /**< 1 ok / 0 fail */

/* ---- class: the calls the Spinel side lacked (doc/codebase_tidy T5) ------ */

int fmrb_spx_app_pid(void);                      /**< FmrbApp.pid (-1 outside an app) */
const char *fmrb_spx_app_script_path(void);      /**< FmrbApp.script_path (:binstr) */
/** @brief FmrbApp.free_bytes(path): 8-byte :binstr (low u32, high u32 LE), ""
 *  when unknown. */
const char *fmrb_spx_app_free_bytes(const char *path, int len);
int fmrb_spx_app_notify(const char *summary, int slen, const char *body, int blen); /**< 1 posted */
/** @brief What fmrb_spx_app_linux_exit_code answers while the program runs. */
#define FMRB_SPX_APP_LINUX_RUNNING (-1000000)
int fmrb_spx_app_linux_available(void);          /**< FmrbApp.linux_available? (1/0) */
/** @brief FmrbApp.linux_start: a handle, or -1 (why: fmrb_spx_app_linux_last_error). */
int fmrb_spx_app_linux_start(int has_cmd, const char *cmd, int clen, const char *cwd, int dlen,
                             int cols, int rows);
const char *fmrb_spx_app_linux_last_error(void); /**< why the last linux_start failed */
const char *fmrb_spx_app_linux_read(int id);     /**< output so far (:binstr) */
int fmrb_spx_app_linux_read_ended(void);         /**< 1 when the last read found the program ended */
int fmrb_spx_app_linux_write(int id, const char *str, int len); /**< bytes written, <0 error */
int fmrb_spx_app_linux_signal(int id, int sig);  /**< sig 0 int 1 term 2 kill 3 hup; 1 sent */
int fmrb_spx_app_linux_exit_code(int id);        /**< code, or FMRB_SPX_APP_LINUX_RUNNING */
int fmrb_spx_app_linux_resize(int id, int cols, int rows); /**< 1 done */
int fmrb_spx_app_linux_close(int id);            /**< returns 0 */
int fmrb_spx_app_wifi_connect(const char *ssid, int slen, const char *pw, int plen); /**< 1 done */
int fmrb_spx_app_wifi_disconnect(void);          /**< 1 done */
int fmrb_spx_app_wifi_forget(const char *ssid, int len); /**< 1 done */
int fmrb_spx_app_bt_power(int on);               /**< 1 done */
int fmrb_spx_app_bt_connect(const char *addr, int len, int on); /**< 1 done */

/** @brief Signal boot complete (status LED: fast-blink -> heartbeat). The mruby
 *  desktop calls FmrbKernel.boot_complete!; the Spinel desktop routes here.
 *  Returns 0. */
int fmrb_spx_app_boot_complete(void);

#ifdef __cplusplus
}
#endif

#endif /* FMRB_SPX_APP_H */
