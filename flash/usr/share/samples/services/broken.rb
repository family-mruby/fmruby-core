# A service that raises on purpose.
#
# It exists so the isolation can be checked rather than assumed: copy it to
# /home/services/, list it in /home/services.toml with enable = true, and the
# log must show three errors from broken, then one line saying it has been
# switched off -- while the other services go on running. If they stop too,
# the host is at fault, not this file.
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
