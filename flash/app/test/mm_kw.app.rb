# Riskier syntax kept apart from mm_check so a compile failure here does not
# hide the basic results: **kw in method_missing, and keyword parameters
# (required / defaulted) on a normal def. Results go to the log as "MMKW".
class KwProxy
  def method_missing(name, *args, **kw, &blk)
    [:mm, name, args, kw]
  end
end

class KwTarget
  def move_to(x:, y: 5)
    [x, y]
  end
end

class MmKwApp < FmrbApp
  def on_create
    @pass = 0
    @fail = 0

    begin
      p = KwProxy.new
      r = p.move_to(1, x: 2)
      expect("mm **kw", [r[0], r[1], r[2], r[3][:x]], [:mm, :move_to, [1], 2])
    rescue => e
      @fail += 1
      Log.error("MMKW NG mm **kw: #{e.class}: #{e.message}")
    end

    begin
      expect("def kwargs", KwTarget.new.move_to(x: 1), [1, 5])
    rescue => e
      @fail += 1
      Log.error("MMKW NG def kwargs: #{e.class}: #{e.message}")
    end

    Log.info("MMKW DONE pass=#{@pass} fail=#{@fail}")
    clear_user_area
    @gfx.draw_text(@user_area_x0 + 4, @user_area_y0 + 4,
                   "MMKW pass=#{@pass} fail=#{@fail}", theme_fg)
    @gfx.present
  end

  def expect(label, got, want)
    if got == want
      @pass += 1
      Log.info("MMKW ok #{label}")
    else
      @fail += 1
      Log.error("MMKW NG #{label}: got=#{got.inspect} want=#{want.inspect}")
    end
  end

  def on_update
    1000
  end
end

begin
  app = MmKwApp.new
  app.start
rescue => e
  Log.error("MmKwApp: #{e}")
end
