# set_timer smoke test (mruby engine): two one-shot timers + clear_time.
class TimerTestApp < FmrbApp
  def on_create
    @t0 = Machine.board_millis
    id1 = set_timer(500) { Log.info("TIMER1 fired at +#{Machine.board_millis - @t0}ms") }
    id2 = set_timer(1500) { Log.info("TIMER2 fired at +#{Machine.board_millis - @t0}ms") }
    id3 = set_timer(1000) { Log.info("TIMER3 should never fire") }
    clear_time(id3)
    Log.info("TIMERS armed: #{id1} #{id2} (cleared #{id3})")
  end

  def on_update
    100
  end
end

begin
  app = TimerTestApp.new
  app.start
rescue => e
  Log.error("timer_test: #{e.message}")
end
