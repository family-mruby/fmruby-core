# NTSC Color Test - Display test patterns for CVBS output calibration
# Left click: next page (or adjust on pages 7-8)
# Right click: previous page
# 8 test pages for verifying NTSC signal quality

class NtscColorTestApp < FmrbApp
  NUM_PAGES = 8

  def on_create
    @page = 0
    @output_level = 128
    @chroma_level = 128
    @w = @user_area_width
    @h = @user_area_height
    @status_h = 12
    draw_page
  end

  def on_event(ev)
    super(ev)
    if ev[:type] == :mouse_down
      x = ev[:x]
      y = ev[:y]
      btn = ev[:button]

      if btn == 1 # left click
        if @page >= 6 && y < @h - @status_h
          handle_adjust_click(x, y)
        else
          @page = (@page + 1) % NUM_PAGES
        end
        draw_page
      elsif btn == 3 # right click
        @page = (@page - 1) % NUM_PAGES
        draw_page
      end
    end
  end

  def draw_page
    case @page
    when 0 then draw_white_on_black
    when 1 then draw_black_on_white
    when 2 then draw_black_on_gray
    when 3 then draw_color_bars
    when 4 then draw_grayscale
    when 5 then draw_color_grid
    when 6 then draw_output_level_ui
    when 7 then draw_chroma_level_ui
    end
    draw_status_bar
    @gfx.present
  end

  # Page 0: White text on black background
  def draw_white_on_black
    @gfx.clear(0x00)
    y = 4
    sz = 1
    while sz <= 4
      @gfx.set_text_size(sz)
      @gfx.draw_text(4, y, "White on Black sz=#{sz}", 0xFF)
      y += sz * 10 + 4
      sz += 1
    end
  end

  # Page 1: Black text on white background
  def draw_black_on_white
    @gfx.clear(0xFF)
    y = 4
    sz = 1
    while sz <= 4
      @gfx.set_text_size(sz)
      @gfx.draw_text(4, y, "Black on White sz=#{sz}", 0x00, 0xFF)
      y += sz * 10 + 4
      sz += 1
    end
  end

  # Page 2: Black text on gray (0xDB) background
  def draw_black_on_gray
    @gfx.clear(0xDB)
    y = 4
    sz = 1
    while sz <= 4
      @gfx.set_text_size(sz)
      @gfx.draw_text(4, y, "Black on Gray sz=#{sz}", 0x00, 0xDB)
      y += sz * 10 + 4
      sz += 1
    end
  end

  # Page 3: Color bars (8 primary colors in RGB332)
  def draw_color_bars
    @gfx.clear(0x00)
    colors = [
      [0x00, "Blk"], [0xE0, "Red"], [0x1C, "Grn"], [0x03, "Blu"],
      [0xFC, "Yel"], [0xE3, "Mag"], [0x1F, "Cyn"], [0xFF, "Wht"]
    ]
    bar_w = @w / colors.length
    cn = colors.length
    i = 0
    while i < cn
      c = colors[i]
      x = i * bar_w
      @gfx.fill_rect(x, 0, bar_w, @h - @status_h, c[0])
      i += 1
    end
    @gfx.set_text_size(1)
    i = 0
    while i < cn
      c = colors[i]
      x = i * bar_w + 1
      tc = (c[0] == 0x00 || c[0] == 0x03) ? 0xFF : 0x00
      @gfx.draw_text(x, @h - @status_h - 12, c[1], tc)
      i += 1
    end
  end

  # Page 4: Grayscale ramp
  def draw_grayscale
    @gfx.clear(0x00)
    grays = [0x00, 0x49, 0x92, 0xDB, 0xFF]
    bar_h = (@h - @status_h - 4) / grays.length
    @gfx.set_text_size(1)
    i = 0
    gn = grays.length
    while i < gn
      g = grays[i]
      y = i * bar_h
      @gfx.fill_rect(0, y, @w, bar_h, g)
      tc = g < 0x80 ? 0xFF : 0x00
      hex = g.to_s(16)
      hex = "0" + hex if hex.length < 2
      @gfx.draw_text(4, y + 2, "0x#{hex} (#{g})", tc)
      i += 1
    end
  end

  # Page 5: Color grid (R x G, two panels for B=0 and B=3)
  def draw_color_grid
    @gfx.clear(0x00)
    @gfx.set_text_size(1)
    @gfx.draw_text(2, 0, "R(row)xG(col) B=0", 0xFF)

    half_w = @w / 2
    @gfx.draw_text(half_w + 2, 0, "B=3", 0xFF)

    cell = 6
    top = 12
    r = 0
    while r < 8
      g = 0
      while g < 8
        c0 = (r << 5) | (g << 2) | 0
        @gfx.fill_rect(g * cell + 2, top + r * cell, cell, cell, c0)
        c3 = (r << 5) | (g << 2) | 3
        @gfx.fill_rect(half_w + g * cell + 2, top + r * cell, cell, cell, c3)
        g += 1
      end
      r += 1
    end
  end

  # Page 6: Output Level adjustment UI
  def draw_output_level_ui
    draw_adjust_ui("Output Level", @output_level)
  end

  # Page 7: Chroma Level adjustment UI
  def draw_chroma_level_ui
    draw_adjust_ui("Chroma Level", @chroma_level)
  end

  def draw_adjust_ui(title, value)
    @gfx.clear(0xDB)
    @gfx.set_text_size(2)
    @gfx.draw_text(10, 10, title, 0x00, 0xDB)

    @gfx.set_text_size(3)
    @gfx.draw_text(10, 40, value.to_s, 0x00, 0xDB)

    btn_y = 80
    btn_w = 50
    btn_h = 30

    # [-16] button
    @gfx.fill_rect(10, btn_y, btn_w, btn_h, 0xE0)
    @gfx.set_text_size(2)
    @gfx.draw_text(18, btn_y + 6, "-16", 0xFF, 0xE0)

    # [-1] button
    @gfx.fill_rect(70, btn_y, btn_w, btn_h, 0xE0)
    @gfx.draw_text(82, btn_y + 6, "-1", 0xFF, 0xE0)

    # [+1] button
    @gfx.fill_rect(130, btn_y, btn_w, btn_h, 0x1C)
    @gfx.draw_text(140, btn_y + 6, "+1", 0xFF, 0x1C)

    # [+16] button
    @gfx.fill_rect(190, btn_y, btn_w, btn_h, 0x1C)
    @gfx.draw_text(196, btn_y + 6, "+16", 0xFF, 0x1C)

    # Preview bar
    bar_y = btn_y + btn_h + 10
    bar_w = @w - 20
    fill_w = value * bar_w / 255
    @gfx.fill_rect(10, bar_y, bar_w, 10, 0x00)
    @gfx.fill_rect(10, bar_y, fill_w, 10, 0xFF)
  end

  def handle_adjust_click(x, y)
    btn_y = 80
    btn_h = 30
    return unless y >= btn_y && y < btn_y + btn_h

    delta = 0
    if x >= 10 && x < 60
      delta = -16
    elsif x >= 70 && x < 120
      delta = -1
    elsif x >= 130 && x < 180
      delta = 1
    elsif x >= 190 && x < 240
      delta = 16
    end
    return if delta == 0

    if @page == 6
      @output_level = [[@output_level + delta, 0].max, 255].min
      @gfx.set_output_level(@output_level)
    elsif @page == 7
      @chroma_level = [[@chroma_level + delta, 0].max, 255].min
      @gfx.set_chroma_level(@chroma_level)
    end
  end

  def draw_status_bar
    y = @h - @status_h
    @gfx.fill_rect(0, y, @w, @status_h, 0x00)
    @gfx.set_text_size(1)
    @gfx.draw_text(2, y + 2, "P#{@page + 1}/#{NUM_PAGES} L:next R:prev", 0xFF)
  end
end

begin
  app = NtscColorTestApp.new
  Log.info("NtscColorTestApp created")
  app.start
rescue => e
  Log.error("Exception: #{e.message}")
end
