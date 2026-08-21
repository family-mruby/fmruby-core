#---fmrb
# default_window_mode = "fullscreen
# this fence is never closed, and the string above is unterminated
# Negative test (doc/multivm_app/instruction_m1.md T1): a broken fence must
# warn and fall back to the default attributes, not fail the spawn.
class FenceBadApp < FmrbApp
  def on_create
    Log.info("FENCE: bad-fence app up, area=#{@user_area_width}x#{@user_area_height}")
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                   "default attrs", theme_fg)
    draw_window_frame
    @gfx.present
  end

  def on_update
    100
  end
end

begin
  FenceBadApp.new.start
rescue => e
  Log.error("FENCE: exception #{e.class}: #{e.message}")
end
