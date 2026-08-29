# A Ruby program that holds the CPU: it never blocks, never yields, and never
# returns from on_update for 20 seconds. Used to check that the rest of the
# system stays usable while it runs (doc/wasm/report/p2.md).
#
# On the Linux simulation the desktop is also protected by FreeRTOS priority
# (a user app is 2, the desktop 8), so this app passing is necessary rather
# than sufficient; slice_probe is the one that exercises preemption inside
# the VM.

class SpinHogApp < FmrbApp
  SPIN_MS = 20000

  def on_create
    clear_user_area
    @gfx.draw_text(8, 8, "spin_hog: burning CPU", theme_fg)
    @gfx.present
    Log.info("spin_hog: start")
  end

  def on_update
    t0 = Machine.board_millis
    n = 0
    while Machine.board_millis - t0 < SPIN_MS
      n += 1
    end
    Log.info("spin_hog: #{n} iterations in #{Machine.board_millis - t0}ms")
    1000
  end
end

begin
  app = SpinHogApp.new
  app.start
rescue => e
  Log.error("spin_hog: #{e.class}: #{e.message}")
end
