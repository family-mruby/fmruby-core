# A service that raises on purpose, shipped switched off.
#
# It exists so the isolation can be checked rather than assumed: turn it on in
# /home/services.toml (enable = true), and the log must show three errors from
# broken, then one line saying it has been switched off -- while heartbeat
# goes on beating. If the other services stop too, the host is at fault, not
# this file.
#
#   [broken]
#   file = "broken.rb"
#   class = "BrokenService"
#   interval_ms = 2000
#   enable = false
class BrokenService
  def on_start(ctx)
    @ctx = ctx
    @n = 0
    ctx.log("loaded; every tick from here raises")
  end

  def on_tick(now_ms)
    @n += 1
    raise "broken on purpose (tick #{@n})"
  end
end
