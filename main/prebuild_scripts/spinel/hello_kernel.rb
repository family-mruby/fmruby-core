# Minimal Spinel-compiled kernel bring-up program (Phase 1 T1-6).
#
# Proves the fmrb_spx FFI shim works from Spinel-generated code running as a
# FreeRTOS task: it logs, reads board_millis a few times, then polls the
# kernel message queue a bounded number of times and logs what it saw.
#
# Compiled with `spinel --no-main --entry hello_kernel_entry -c` (see
# compile_ruby_to_spinel.cmake). The C entry is invoked by a wrapper task in
# fmrb_kernel.c when FMRB_KERNEL_ENGINE=spinel.
#
# require_relative is spliced at parse time by Spinel.
require_relative "fmrb_ffi"

def log(level, msg)
  FmrbSpx.fmrb_spx_log_write(level, msg, msg.bytesize)
end

# --- announce + board_millis smoke ---
log(1, "spinel hello")

i = 0
while i < 3
  ms = FmrbSpx.fmrb_spx_board_millis
  log(1, "board_millis[#{i}]=#{ms}")
  i += 1
end

# --- poll the kernel queue up to 10 times (100 ms each), log each result ---
buf = FmrbSpx.msg_buf
tout = FmrbSpx.type_out
sout = FmrbSpx.src_out

polls = 0
messages = 0
while polls < 10
  len = FmrbSpx.fmrb_spx_recv_message(buf, 192, 100, tout, sout)
  if len > 0
    type = FmrbSpx.read_i32(tout)
    src = FmrbSpx.read_i32(sout)
    # Payload bytes live in the :ptr buffer; byte-level parsing (getbyte) needs
    # a String, so Phase 2 will read fields via ffi_read_* or copy the buffer.
    log(1, "msg len=#{len} type=#{type} src=#{src}")
    messages += 1
  elsif len == 0
    log(0, "poll #{polls}: timeout")
  else
    log(2, "poll #{polls}: recv error #{len}")
  end
  polls += 1
end

log(1, "spinel hello done: polls=#{polls} messages=#{messages}")
