# Debugger sample app
#
# A small app made for exercising the remote debugger (doc/vm_remote_debug_*):
# it ticks continuously, calls through a 3-level method chain, and keeps
# locals of every basic type in scope so breakpoints, stepping, stack traces
# and the variables pane all have something to show.
#
# Suggested breakpoints (marked BP-1..BP-4 below):
#   BP-1  on_update       - hits every 200ms; watch @tick grow per continue
#   BP-2  compute         - one local of each type; try step in/over/out here
#   BP-3  helper_double   - deepest frame; check the call stack, then step out
#   BP-4  on_event        - hits only when you click inside the window
#
# The window keeps drawing tick/total while running, so a parked (stopped)
# VM is immediately visible: the numbers freeze, the rest of the desktop
# keeps moving.

class DbgSampleApp < FmrbApp
  def on_create
    @tick = 0
    @total = 0
    @phase = :odd
    @last_click = "(none)"
    draw_screen
  end

  def on_update
    @tick += 1                                # BP-1: periodic hit
    value = compute(@tick)
    @total += value
    @phase = (@tick % 2 == 0) ? :even : :odd
    draw_screen if @tick % 5 == 0
    200
  end

  def on_event(ev)
    return unless ev[:type] == :mouse_up
    x = ev[:x]
    y = ev[:y]
    @last_click = "(#{x},#{y})"               # BP-4: click inside the window
    draw_screen
  end

  private

  # Stack: on_update -> compute -> helper_double. Locals cover every type
  # the variables pane formats (Integer/Float/String/Symbol/bool/nil/
  # Array/Hash/Object).
  def compute(n)
    doubled  = helper_double(n)
    ratio    = doubled / 3.0
    label    = "tick=#{n}"
    tag      = :sample
    flag     = (doubled % 4 == 0)
    nothing  = nil
    items    = [n, doubled, label]
    info     = { "n" => n, "doubled" => doubled }
    long_txt = "0123456789" * 10
    result   = doubled + items.size           # BP-2: inspect the locals above
    result
  end

  def helper_double(n)
    m = n * 2                                 # BP-3: deepest frame; step out
    m
  end

  def draw_screen
    return unless @gfx
    clear_user_area

    x = @user_area_x0 + 4
    y = @user_area_y0 + 4
    @gfx.draw_text(x, y, "Debugger sample", theme_fg)
    y += 14
    @gfx.draw_text(x, y, "tick : #{@tick}", theme_accent)
    y += 12
    @gfx.draw_text(x, y, "total: #{@total}", theme_accent)
    y += 12
    @gfx.draw_text(x, y, "phase: #{@phase}", theme_border)
    y += 12
    @gfx.draw_text(x, y, "click: #{@last_click}", theme_fg)
    y += 16
    @gfx.draw_text(x, y, "Set BPs in dbg_sample.app.rb", theme_border)

    draw_window_frame
    @gfx.present
  end
end

begin
  app = DbgSampleApp.new
  app.start
rescue => e
  Log.error("DbgSampleApp: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
