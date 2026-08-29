# Measures the mruby-task timeslice from Ruby, in whichever way the build
# supplies its ticks (the mruby_tick top-half, or the VM itself with
# FMRB_TASK_SELF_TICK=1 -- see doc/wasm/report/p2.md).
#
# Two CPU-bound Tasks of equal priority in one VM. Neither ever blocks, so the
# only thing that can take the CPU from one and give it to the other is
# mrb->task.switching being set at a timeslice boundary. Each spinner watches
# the wall clock between consecutive iterations: a jump means it was away, and
# the size of the jump is how long the other one ran.
#
# In the log:
#   slice_probe A: n=30 min=.. med=.. max=.. total=..ms
#
# med is the timeslice: MRB_TICK_UNIT * MRB_TIMESLICE_TICK_COUNT milliseconds
# (5 * 10 = 50 in this tree). If a build cannot preempt a spinning task at all,
# the second spinner never runs and nothing is logged -- which is the failure
# this probe exists to catch.

SLICE_SAMPLES = 30

class SliceProbeApp < FmrbApp
  def on_create
    clear_user_area
    @gfx.draw_text(8, 8, "slice_probe running", theme_fg)
    @gfx.present
    Log.info("slice_probe: start")
    spin("A")
    spin("B")
  end

  def spin(tag)
    Task.new(name: "slice_#{tag}", priority: 128) do
      gaps = []
      prev = Machine.board_millis
      t0 = prev
      # A gap of more than 2ms between two iterations of this loop means the
      # scheduler was elsewhere; nothing in the loop itself can take that long.
      while gaps.size < SLICE_SAMPLES
        now = Machine.board_millis
        d = now - prev
        gaps << d if d > 2
        prev = now
      end
      total = Machine.board_millis - t0

      # picoruby has no Array#sum / #min / #max worth relying on.
      i = 0
      mn = gaps[0]
      mx = gaps[0]
      while i < gaps.size
        v = gaps[i]
        mn = v if v < mn
        mx = v if v > mx
        i += 1
      end
      sorted = gaps.sort
      med = sorted[gaps.size / 2]

      Log.info("slice_probe #{tag}: n=#{gaps.size} min=#{mn} med=#{med} max=#{mx} total=#{total}ms")
    end
  end

  # Short, so the root-context main loop reaches Task.pass promptly and hands
  # the VM's scheduler over to the spinners.
  def on_update
    50
  end
end

begin
  app = SliceProbeApp.new
  app.start
rescue => e
  Log.error("slice_probe: #{e.class}: #{e.message}")
end
