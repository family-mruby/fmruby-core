#---fmrb
# default_window_mode = "fullscreen"
#---
# Cell-width probe for the Japanese fonts (doc/editor_ja/instruction_ja1.md T2).
# Draws the same ruler three ways and logs where each row ends, so the advance
# each font gives half-width katakana can be measured rather than assumed.
# Ctrl+Q quits.
class JaWidthApp < FmrbApp
  ROWS = [
    "ABCDE|",        # 6 half cells
    "あいうえお|",    # 5 full + 1 half
    "ｱｲｳｴｵ|",        # 5 half-width katakana + 1 half
  ]

  def on_create
    clear_user_area
    x0 = @user_area_x0 + 4
    y = @user_area_y0 + 4

    @gfx.draw_text(x0, y, "mixed (Font0 + misaki_8)", FmrbGfx::WHITE)
    y += 10
    ROWS.each do |s|
      @gfx.draw_text(x0, y, s, FmrbGfx::WHITE, nil, mixed: true)
      Log.info("JAW: mixed #{s.inspect} i18n=#{FmrbI18n.text_width(s)}")
      y += 10
    end

    y += 6
    @gfx.draw_text(x0, y, "ja 12 (efontJA_12)", FmrbGfx::WHITE)
    y += 12
    @gfx.set_font(:ja, 12)
    ROWS.each do |s|
      @gfx.draw_text(x0, y, s, FmrbGfx::WHITE)
      Log.info("JAW: ja12 #{s.inspect} gfx=#{@gfx.text_width(s)}")
      y += 12
    end
    @gfx.set_font(:default)

    @gfx.present
  end

  def on_update
    100
  end
end

begin
  JaWidthApp.new.start
rescue => e
  Log.error("JAW: #{e.class}: #{e.message}")
end
