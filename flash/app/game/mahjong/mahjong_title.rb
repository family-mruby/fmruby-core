# Mahjong table: the title "ヒデ雀" (MJ8, doc/ruby_asterism/report/mj8.md).
# Part of MahjongApp, read when the app opens (title_draw in
# mahjong_panels.rb); title_unload takes these methods out of the class
# again when the title goes, so a game's VM does not hold them.

class MahjongApp < FmrbApp
  TITLE_TILES = [30, 31, 32, 33, 34, 35, 36] # the winds and the dragons

  # Drawn once (again only when the screen's size changes).
  def draw_title
    return if @title_drawn == @w
    @title_drawn = @w
    @buttons = []
    clear_user_area(C_FELT)
    big = @big ? 4 : 3 # the text scale stops at 4
    tw_ = @xl ? 84 : (@big ? 42 : 24)
    th = @xl ? 112 : (@big ? 56 : 32)
    fnt(16, big)
    title = "ヒデ雀"
    gap = u(@big ? 16 : 8)
    top = @y0 + (@h - (@fw + gap + th + gap + u(12))) / 2
    text(@x0 + (@w - tw(title)) / 2, top, title)
    y = top + @fw + gap
    x = @x0 + (@w - TITLE_TILES.size * tw_) / 2
    i = 0
    while i < TITLE_TILES.size
      c = TITLE_TILES[i]
      if @xl
        xl_draw_item(xl_hand_item(c, x + i * tw_, y, 0))
      elsif @big
        big_tile(c, x + i * tw_, y)
      else
        small_tile(c, x + i * tw_, y)
      end
      i += 1
    end
    fnt(12, @k)
    sub = "Hide Jong"
    text(@x0 + (@w - tw(sub)) / 2, y + th + gap, sub)
    @gfx.present
  end

  def list_ready?
    return false unless @tc
    i = 0
    while i < @tc.table_list.size
      st = @tc.tables[@tc.table_list[i]]
      return true if st && st["st"]
      i += 1
    end
    false
  end

  def title_unload
    self.class.remove_method(:draw_title)
    self.class.remove_method(:list_ready?)
    self.class.remove_method(:title_unload)
  rescue => e
    Log.info("mahjong: title: #{e.message}")
  end
end
