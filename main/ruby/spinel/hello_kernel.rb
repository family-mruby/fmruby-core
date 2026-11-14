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
tout = FmrbSpx.type_out
sout = FmrbSpx.src_out

polls = 0
messages = 0
while polls < 10
  data = FmrbSpx.fmrb_spx_recv_message(100, tout, sout)
  type = FmrbSpx.read_i32(tout)
  if type >= 0
    src = FmrbSpx.read_i32(sout)
    log(1, "msg len=#{data.bytesize} type=#{type} src=#{src} b0=#{data.bytesize > 0 ? data.getbyte(0) : -1}")
    messages += 1
  else
    log(0, "poll #{polls}: timeout")
  end
  polls += 1
end

log(1, "spinel hello done: polls=#{polls} messages=#{messages}")
