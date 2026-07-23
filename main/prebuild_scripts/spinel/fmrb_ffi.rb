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
  # recv fills msg_buf with the payload and writes type/src into the out
  # buffers; returns payload length (>=0), 0 on timeout (type=-1), or negative.
  ffi_func :fmrb_spx_recv_message, [:ptr, :int, :int, :ptr, :ptr], :int
  ffi_func :fmrb_spx_send_raw, [:int, :int, :ptr, :int], :int
  ffi_func :fmrb_spx_try_send_raw, [:int, :int, :ptr, :int], :int

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

  # --- scratch buffers / out-params ---
  # Payload buffer sized to the message payload max (fmrb_msg.h: 176). Round up.
  ffi_buffer :msg_buf, 192
  ffi_buffer :type_out, 4
  ffi_buffer :src_out, 4
  # Window snapshot: FMRB_MAX_APPS (<=7) records * 48 bytes.
  ffi_buffer :win_buf, 384

  ffi_read_i32 :read_i32, 0   # FmrbSpx.read_i32(buf) -> int32 at offset 0
end
