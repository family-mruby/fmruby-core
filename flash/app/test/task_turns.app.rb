# Checks that a Task gets its turns while the app's main loop holds the CPU
# for longer than a timeslice at a time.
#
# The main loop waits in _spin for longer than a timeslice (on_update returns
# BLOCK_MS) and the "passer" Task yields with Task.pass after every step, so
# it is always ready when the main loop's turn ends. With the self-supplied tick
# (FMRB_TASK_SELF_TICK=1, the native and browser builds) the slice that ran
# out during the main loop's turn used to be handed to the next task to run:
# the passer was sent back at its first instruction, every time, and never
# made a step (doc/checkpoint/report/hang_task_sleep.md).

class TaskTurnsApp < FmrbApp
  STEPS = 10
  BLOCK_MS = 120   # the main loop's wait, longer than a timeslice on every build

  def on_create
    clear_user_area
    @gfx.draw_text(8, 8, "task_turns running", theme_fg)
    @gfx.present
    Log.info("task_turns: start")
    Task.new(name: "passer", priority: 128) do
      t0 = Machine.board_millis
      n = 0
      while n < STEPS
        n += 1
        Log.info("task_turns: step #{n} at +#{Machine.board_millis - t0}ms")
        Task.pass
      end
      Log.info("task_turns: done #{STEPS} steps in #{Machine.board_millis - t0}ms")
    end
  end

  def on_update
    BLOCK_MS
  end
end

begin
  app = TaskTurnsApp.new
  app.start
rescue => e
  Log.error("task_turns: #{e.class}: #{e.message}")
end
