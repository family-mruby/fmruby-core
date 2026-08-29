# Fixed Ruby workload, timed, so the cost of the self-supplied tick hook in the
# VM dispatch loop can be measured against the default build
# (doc/wasm/report/p2.md). Runs on the root context, where no task switch can
# perturb it; five rounds, so the spread is visible.

class VmBenchApp < FmrbApp
  ITERATIONS = 10000000
  ROUNDS = 15

  def on_create
    clear_user_area
    @gfx.draw_text(8, 8, "vm_bench running", theme_fg)
    @gfx.present
    @round = 0
  end

  def on_update
    if @round < ROUNDS
      t0 = Machine.board_millis
      i = 0
      acc = 0
      while i < ITERATIONS
        acc = acc + i
        i = i + 1
      end
      dt = Machine.board_millis - t0
      Log.info("vm_bench: round #{@round} #{ITERATIONS} iters in #{dt}ms (acc=#{acc})")
      @round += 1
    end
    100
  end
end

begin
  app = VmBenchApp.new
  app.start
rescue => e
  Log.error("vm_bench: #{e.class}: #{e.message}")
end
