#---fmrb
# default_window_mode = fullscreen
# task_stack_kb = "not a number"
#---
# Negative test (doc/multivm_app/instruction_m1.md T1): the fence closes but
# its body is not valid toml (bare word, wrong type). Must warn and fall back
# to the default attributes rather than fail the spawn.
class FenceBad2App < FmrbApp
  def on_create
    Log.info("FENCE: bad-toml app up, area=#{@user_area_width}x#{@user_area_height}")
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                   "default attrs", FmrbGfx::WHITE)
    draw_window_frame
    @gfx.present
  end

  def on_update
    100
  end
end

begin
  FenceBad2App.new.start
rescue => e
  Log.error("FENCE: exception #{e.class}: #{e.message}")
end
