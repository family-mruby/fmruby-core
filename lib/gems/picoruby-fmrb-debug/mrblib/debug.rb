# FMRB::Debug -- on-device debugger API (Phase E1).
#
# The C bindings (ports/esp32/debug.c) provide the raw calls; this layer adds
# the msgpack decoding for the inspect payloads and shapes the poll_event
# tuple into a Hash. See doc/editor_debug/design.md sec 4.2.
#
# picoruby notes (measured): use ::MessagePack (a bare MessagePack inside a
# module resolves as FMRB::Debug::MessagePack and fails); unpack keys are
# Strings ("frames"/"vars"), not symbols; poll_event must use a short timeout
# and never block the UI loop.
module FMRB
  module Debug
    # Event type codes (mirror fmrb_dbg_ev_type_t).
    EV_STOPPED = 0
    EV_RESUMED = 1
    EV_EXITED  = 2

    # Stop reason codes (mirror FMRB_STOP_*).
    REASON = { 0 => :breakpoint, 1 => :step, 2 => :pause }

    # stack_trace(pid, max = 16) -> [{"idx"=>,"func"=>,"file"=>,"line"=>}, ...] or nil
    def self.stack_trace(pid, max = 16)
      raw = _stack_trace_raw(pid, max)
      raw ? ::MessagePack.unpack(raw)["frames"] : nil
    end

    # frame_vars(pid, frame = 0) -> [{"name"=>,"type"=>,"value"=>,"ref"=>,...}, ...] or nil
    def self.frame_vars(pid, frame = 0)
      raw = _frame_vars_raw(pid, frame)
      raw ? ::MessagePack.unpack(raw)["vars"] : nil
    end

    # expand(pid, ref) -> [{"name"=>,...}, ...] or nil
    def self.expand(pid, ref)
      raw = _expand_raw(pid, ref)
      raw ? ::MessagePack.unpack(raw)["vars"] : nil
    end

    # poll_event(timeout_ms = 0) -> Hash or nil.
    #   { type: :stopped/:resumed/:exited, pid:, reason:, bp_id:, line:, file: }
    # reason/bp_id/line/file are only meaningful for :stopped.
    def self.poll_event(timeout_ms = 0)
      a = _poll_event_raw(timeout_ms)
      return nil unless a
      type = case a[0]
             when EV_STOPPED then :stopped
             when EV_RESUMED then :resumed
             when EV_EXITED  then :exited
             else :unknown
             end
      {
        type:   type,
        pid:    a[1],
        reason: REASON[a[2]],
        bp_id:  a[3],
        line:   a[4],
        file:   a[5],
      }
    end
  end
end
