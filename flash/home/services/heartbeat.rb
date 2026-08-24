# The smallest service there is: a clock tick and a line in the log.
#
# Copy this file to start your own. The contract is five optional methods --
# on_start(ctx), on_tick(now_ms), on_wake(now_ms), on_event(topic, data),
# on_stop -- and ctx is the only way out to the rest of the machine
# (ctx.publish / ctx.wake_in / ctx.audio / ctx.log / ctx.now_ms / ctx.config /
# ctx.stop_self). hourly_chime.rb next door uses the other half of it.
#
# Listed in /home/services.toml as:
#
#   [heartbeat]
#   file = "heartbeat.rb"
#   class = "HeartbeatService"
#   interval_ms = 10000
#
# Keep every method short. All services run one after another on one task, so
# time spent here is time the others wait (the host logs a warning past 50 ms).
class HeartbeatService
  def on_start(ctx)
    @ctx = ctx
    @started_at = ctx.now_ms
    @beats = 0
    ctx.log("hello")
  end

  def on_tick(now_ms)
    @beats += 1
    up = (now_ms - @started_at) / 1000
    @ctx.log("beat #{@beats}, up #{up}s")
    nil
  end

  def on_stop
    @ctx.log("bye after #{@beats} beats")
    nil
  end
end
