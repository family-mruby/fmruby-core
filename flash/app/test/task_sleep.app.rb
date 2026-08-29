# Checks that a Task that sleeps still wakes up.
#
# The interesting case is the self-supplied tick (FMRB_TASK_SELF_TICK=1): ticks
# come from the VM's dispatch loop, and a VM whose only task is asleep runs no
# bytecode at all. If the idle path does not supply ticks of its own, the
# scheduler's clock stops and this app hangs after "round 0"
# (doc/wasm/report/p2.md).

class TaskSleepApp < FmrbApp
  ROUNDS = 10
  SLEEP_MS = 100

  def on_create
    clear_user_area
    @gfx.draw_text(8, 8, "task_sleep running", theme_fg)
    @gfx.present
    Log.info("task_sleep: start")
    Task.new(name: "sleeper", priority: 128) do
      t0 = Machine.board_millis
      n = 0
      while n < ROUNDS
        sleep_ms(SLEEP_MS)
        n += 1
        Log.info("task_sleep: round #{n} at +#{Machine.board_millis - t0}ms")
      end
      Log.info("task_sleep: done #{ROUNDS} x #{SLEEP_MS}ms in #{Machine.board_millis - t0}ms")
    end
  end

  def on_update
    50
  end
end

begin
  app = TaskSleepApp.new
  app.start
rescue => e
  Log.error("task_sleep: #{e.class}: #{e.message}")
end
