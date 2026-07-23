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
  ffi_func :fmrb_spx_windows_snapshot, [:ptr, :int], :int
  ffi_func :fmrb_spx_set_hid_target, [:int], :int
  ffi_func :fmrb_spx_set_focused_window, [:int], :int
  ffi_func :fmrb_spx_bring_to_front, [:int], :int
  ffi_func :fmrb_spx_update_window_pos, [:int, :int, :int], :int
  ffi_func :fmrb_spx_update_window_size, [:int, :int, :int], :int
  ffi_func :fmrb_spx_suspend_app, [:int], :int
  ffi_func :fmrb_spx_resume_app, [:int], :int
  ffi_func :fmrb_spx_reap_app, [:int], :int
  # spawn by name (:str + explicit length); returns new PID or negative.
  ffi_func :fmrb_spx_spawn_app_req, [:str, :int], :int

  # --- app/system info + boot handshake (Phase 2) ---
  # Fill a packed app-info record; returns bytes written (>=0) or negative.
  ffi_func :fmrb_spx_app_info_snapshot, [:int, :ptr, :int], :int
  # Fill last-error name/message into the buffer (NUL-separated); returns bytes
  # written (>0) or 0 if no error.
  ffi_func :fmrb_spx_last_error, [:ptr, :int], :int
  ffi_func :fmrb_spx_set_error_led, [:int], :int
  ffi_func :fmrb_spx_set_ready, [], :int
  ffi_func :fmrb_spx_check_protocol_version, [:int], :int  # 1 ok / 0 fail / neg err
  ffi_func :fmrb_spx_check_ga_version, [:int], :int
  ffi_func :fmrb_spx_sync_time_to_host, [], :int

  # --- scratch buffers / out-params ---
  # Payload buffer sized to the message payload max (fmrb_msg.h: 176). Round up.
  ffi_buffer :msg_buf, 192
  ffi_buffer :type_out, 4
  ffi_buffer :src_out, 4
  # Window snapshot: FMRB_MAX_APPS (<=7) records * 48 bytes.
  ffi_buffer :win_buf, 384
  # App-info / last-error packed record (<= payload max).
  ffi_buffer :info_buf, 192

  ffi_read_i32 :read_i32, 0   # FmrbSpx.read_i32(buf) -> int32 at offset 0
end
