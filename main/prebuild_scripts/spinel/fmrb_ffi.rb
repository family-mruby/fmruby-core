# Spinel FFI declarations for the fmrb_spx kernel shim (main/kernel/fmrb_spx.h).
#
# These bind the mrb-free C ABI so Spinel-compiled kernel code can call it.
# Per Spinel docs/FFI.md the ffi_* declarations must live directly in the
# module body. The C side is implemented in main/kernel/fmrb_spx_kernel.c.
#
# Buffer / out-parameter convention (see fmrb_spx.h): structured data crosses
# as fixed-layout byte buffers filled by C and read back with ffi_read_* /
# getbyte -- never boxed into a symbol Hash on a hot path (keeps values typed,
# not poly; Phase 0 finding).

module FmrbSpx
  # --- scalar / logging ---
  ffi_func :fmrb_spx_board_millis, [], :int
  ffi_func :fmrb_spx_log_write, [:int, :str, :int], :void
  ffi_func :fmrb_spx_theme_color, [:int], :int   # theme colour by fmrb_theme_t field index

  # --- messaging ---
  # recv returns the payload as a binary-safe String (:binstr; byte-exact via
  # sp_net_bin_len, so embedded NUL survives) and writes type/src into the out
  # buffers. On timeout the String is empty and type_out = -1.
  ffi_func :fmrb_spx_recv_message, [:int, :ptr, :ptr], :binstr
  # send: the payload is passed as :str (pointer to the String's byte buffer)
  # plus an explicit length, so embedded NUL bytes are preserved (C memcpy's
  # `len` bytes, not strlen). Verified: a "\x04\x01\x00\x00\x00\x00" packet
  # arrives byte-exact.
  ffi_func :fmrb_spx_send_raw, [:int, :int, :str, :int], :int
  ffi_func :fmrb_spx_try_send_raw, [:int, :int, :str, :int], :int

  # --- window / focus / lifecycle ---
  # windows_snapshot returns the packed records as :binstr (a real String read
  # with getbyte); an ffi_buffer :ptr has no getbyte. Byte length = N * 48.
  ffi_func :fmrb_spx_windows_snapshot, [], :binstr
  ffi_func :fmrb_spx_set_hid_target, [:int], :int
  ffi_func :fmrb_spx_set_focused_window, [:int], :int
  ffi_func :fmrb_spx_bring_to_front, [:int], :int
  ffi_func :fmrb_spx_update_window_pos, [:int, :int, :int], :int
  ffi_func :fmrb_spx_update_window_size, [:int, :int, :int], :int
  ffi_func :fmrb_spx_set_app_fullscreen, [:int, :int, :int, :int], :int
  ffi_func :fmrb_spx_suspend_app, [:int], :int
  ffi_func :fmrb_spx_resume_app, [:int], :int
  ffi_func :fmrb_spx_reap_app, [:int], :int
  # spawn by name (:str + explicit length); returns new PID or negative.
  ffi_func :fmrb_spx_spawn_app_req, [:str, :int], :int

  # --- app/system info + boot handshake (Phase 2) ---
  # app_info / last_error also return :binstr packed records (empty String when
  # absent -> Ruby nil), for the same getbyte reason as windows_snapshot.
  ffi_func :fmrb_spx_app_info_snapshot, [:int], :binstr
  ffi_func :fmrb_spx_last_error, [], :binstr
  # file sync: count, then one packed entry (src 128 + dest 128) per index
  ffi_func :fmrb_spx_sync_file_count, [], :int
  ffi_func :fmrb_spx_sync_file_entry, [:int], :binstr
  ffi_func :fmrb_spx_sync_file, [:str, :int, :str, :int], :int
  ffi_func :fmrb_spx_set_error_led, [:int], :int
  ffi_func :fmrb_spx_set_ready, [], :int
  ffi_func :fmrb_spx_check_protocol_version, [:int], :int  # 1 ok / 0 fail / neg err
  ffi_func :fmrb_spx_check_ga_version, [:int], :int
  ffi_func :fmrb_spx_sync_time_to_host, [], :int

  # --- out-params ---
  # recv writes message type / src into these; read back with read_i32. (Window,
  # app-info and last-error records now cross as :binstr Strings, not buffers,
  # because getbyte needs a String -- an ffi_buffer is a :ptr.)
  ffi_buffer :type_out, 4
  ffi_buffer :src_out, 4

  ffi_read_i32 :read_i32, 0   # FmrbSpx.read_i32(buf) -> int32 at offset 0
end
