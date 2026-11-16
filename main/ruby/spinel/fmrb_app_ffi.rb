# Spinel FFI declarations for the FmrbApp / FmrbGfx shims
# (main/app/fmrb_spx_app.h, main/app/fmrb_spx_gfx.h).
#
# Same conventions as fmrb_ffi.rb (the kernel shim): ffi_* declarations live
# directly in the module body; structured data crosses as fixed-layout byte
# buffers read back with getbyte (:binstr), never boxed into a symbol Hash on a
# hot path (Phase 0 finding). The C side is main/app/fmrb_spx_app.c / _gfx.c.
#
# The Spinel system_desktop combined program splices this file together with
# fmrb_ffi.rb (FmrbSpx, reused for Log / board_millis) before
# fmrb_app_base_spinel.rb.

# The app VM reuses only the two generic (non-kernel) FmrbSpx entry points --
# millis + logging (defined in fmrb_spx_common.c). The full kernel FmrbSpx
# (fmrb_ffi.rb) is deliberately NOT spliced into an app program: Spinel emits an
# extern for every declared ffi_func, so splicing it would drag the whole kernel
# shim (recv_message, windows_snapshot, spawn_app_req, ...) into the app's link,
# breaking a mixed mruby-kernel + Spinel-desktop build where fmrb_spx_kernel.c
# is not compiled. Log / Machine in fmrb_app_base_spinel.rb use these two.
module FmrbSpx
  ffi_func :fmrb_spx_board_millis, [], :int
  ffi_func :fmrb_spx_board_micros, [], :int
  ffi_func :fmrb_spx_log_write, [:int, :str, :int], :void
  ffi_func :fmrb_spx_theme_color, [:int], :int   # theme colour by fmrb_theme_t field index
  ffi_func :fmrb_spx_wheel_lines, [], :int       # rows one wheel notch scrolls
end

module FmrbSpxGfx
  # The FmrbGfx methods on the app boundary are declared in the generated
  # module FmrbBoundary (fmrb_gen/fmrb_boundary_ffi.rb, from sig/), sprites and
  # video included. Here is only the drawer, which the engine's FmrbGfx opens,
  # lays out and closes (not on the boundary).

  # --- the drawer: FmrbGfx.new, _set_origin, destroy ---
  ffi_func :fmrb_spx_gfx_drawer_open, [:int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_drawer_set_origin, [:int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_gfx_drawer_close, [:int], :int
end

module FmrbSpxApp
  # --- instance lifecycle ---
  ffi_func :fmrb_spx_app_init, [], :binstr           # 52B ctx snapshot (creates canvas)
  # poll one message; payload as :binstr, type/src via out-params
  ffi_func :fmrb_spx_app_recv_message, [:int, :ptr, :ptr], :binstr
  ffi_func :fmrb_spx_app_cleanup, [], :int
  ffi_func :fmrb_spx_app_mark_expected_stop, [], :int
  ffi_func :fmrb_spx_app_send_message, [:int, :int, :str, :int], :int
  ffi_func :fmrb_spx_app_set_window_param, [:int, :int], :int  # 0=pos_x 1=pos_y
  # hit shape of a frameless window (fmrb_app.h layout): 1 ok / 0 refused
  ffi_func :fmrb_spx_app_set_hit_shape, [:str, :int], :int
  ffi_func :fmrb_spx_app_hit_sprite_place, [:int, :int, :int, :int], :int
  ffi_func :fmrb_spx_app_is_file_app, [], :int
  ffi_func :fmrb_spx_app_create_canvas, [:int, :int, :int, :int, :int], :int
  ffi_func :fmrb_spx_app_delete_canvas, [:int], :int

  # --- class: process / memory info (all :binstr) ---
  ffi_func :fmrb_spx_app_ps, [], :binstr
  ffi_func :fmrb_spx_app_heap_info, [], :binstr
  ffi_func :fmrb_spx_app_pool_usage, [], :int
  ffi_func :fmrb_spx_app_pool_used, [], :int
  ffi_func :fmrb_spx_app_last_error, [], :binstr

  # --- class: configuration / clock ---
  ffi_func :fmrb_spx_app_config, [:str, :int], :binstr
  ffi_func :fmrb_spx_app_set_kana_mode, [:int], :int
  ffi_func :fmrb_spx_app_clipboard_length, [], :int
  ffi_func :fmrb_spx_app_clipboard_chunk, [:int, :int], :binstr
  ffi_func :fmrb_spx_file_length, [:int, :str, :int], :int
  ffi_func :fmrb_spx_file_chunk, [:int, :str, :int, :int], :binstr
  ffi_func :fmrb_spx_app_rd_stream_state, [], :int
  ffi_func :fmrb_spx_app_set_wallclock, [:int, :int, :int, :int, :int, :int], :binstr

  # --- class: cursor / power ---
  ffi_func :fmrb_spx_app_enable_cursor, [], :int
  ffi_func :fmrb_spx_app_set_cursor_visible, [:int], :int
  ffi_func :fmrb_spx_app_reboot, [], :int
  ffi_func :fmrb_spx_app_power_actions, [], :int
  ffi_func :fmrb_spx_app_power, [:int], :int
  ffi_func :fmrb_spx_app_ble_start, [], :int
  ffi_func :fmrb_spx_app_ble_state, [], :int
  ffi_func :fmrb_spx_app_ble_settings_window, [:int], :int
  ffi_func :fmrb_spx_app_ble_settings_left, [], :int
  ffi_func :fmrb_spx_app_meta, [:str, :int], :binstr
  ffi_func :fmrb_spx_app_move, [:int, :str, :int, :int], :binstr
  ffi_func :fmrb_spx_app_checkpoint, [:int, :int], :binstr
  ffi_func :fmrb_spx_app_set_audio_mute, [:int], :int
  ffi_func :fmrb_spx_app_set_audio_volume, [:int], :int
  ffi_func :fmrb_spx_app_preview_audio_volume, [:int], :int
  ffi_func :fmrb_spx_app_audio_conf_saved, [], :int
  ffi_func :fmrb_spx_app_bt_mac, [], :binstr

  # --- class: network / usb ---
  ffi_func :fmrb_spx_app_wifi_info, [], :binstr
  ffi_func :fmrb_spx_app_wifi_networks, [:int], :binstr
  ffi_func :fmrb_spx_app_clear_cache, [:str, :int], :binstr
  ffi_func :fmrb_spx_app_usb_devices, [], :binstr
  ffi_func :fmrb_spx_app_hid_raw_subscribe, [:int], :int
  ffi_func :fmrb_spx_app_hid_raw_unsubscribe, [:int], :int
  ffi_func :fmrb_spx_app_wifi_connect, [:str, :int, :str, :int], :int
  ffi_func :fmrb_spx_app_wifi_disconnect, [], :int
  ffi_func :fmrb_spx_app_wifi_forget, [:str, :int], :int
  ffi_func :fmrb_spx_app_bt_power, [:int], :int
  ffi_func :fmrb_spx_app_bt_connect, [:str, :int, :int], :int

  # --- class: process / files / Linux commands (doc/codebase_tidy T5) ---
  ffi_func :fmrb_spx_app_linux_start, [:int, :str, :int, :str, :int, :int, :int], :int
  ffi_func :fmrb_spx_app_linux_last_error, [], :binstr
  ffi_func :fmrb_spx_app_linux_read, [:int], :binstr
  ffi_func :fmrb_spx_app_linux_read_ended, [], :int
  ffi_func :fmrb_spx_app_linux_write, [:int, :str, :int], :int
  ffi_func :fmrb_spx_app_linux_signal, [:int, :int], :int
  ffi_func :fmrb_spx_app_linux_exit_code, [:int], :int
  ffi_func :fmrb_spx_app_linux_resize, [:int, :int, :int], :int
  ffi_func :fmrb_spx_app_linux_close, [:int], :int
  ffi_func :fmrb_spx_app_boot_complete, [], :int
  ffi_func :fmrb_spx_app_gc, [], :int                # force GC on this task's heap

  # --- out-params for recv_message (read back with read_i32) ---
  ffi_buffer :type_out, 4
  ffi_buffer :src_out, 4
  ffi_read_i32 :read_i32, 0
end
