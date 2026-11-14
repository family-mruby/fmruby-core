#---fmrb
# app_screen_name = "MetaComment"
# icon = "app/test/othello.icon"
# default_window_mode = "window"
# default_window_width = 200
# default_window_height = 120
# default_window_pos_x = 10
#---
# Metadata merge test (doc/codebase_tidy T1): this file has both a comment
# fence and an .app.toml. They are merged key by key and the .app.toml wins a
# key both set. Expected: the launcher shows "MetaMerge" with the othello
# icon (icon only in the comment), and the window is 200x120 (size only in
# the comment) at x = 60 (the .app.toml's pos_x beats the comment's 10).
class MetaMergeApp < FmrbApp
  def on_create
    Log.info("META: merge app up, window=#{@window_width}x#{@window_height} at #{@pos_x},#{@pos_y}")
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4, "MetaMerge", theme_fg)
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 18,
                   "#{@window_width}x#{@window_height} at #{@pos_x},#{@pos_y}", theme_fg)
    @gfx.present
  end
end

begin
  MetaMergeApp.new.start
rescue => e
  Log.error("MetaMergeApp: #{e.class}: #{e.message}")
end
