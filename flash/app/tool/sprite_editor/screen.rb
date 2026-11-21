# Sprite Editor (doc/asterism_chat C3a): the layout and the drawing,
# required by sprite_editor.app.rb after its class body.
#
# The area, top to bottom: the menu bar, the body, the status line. The
# body, left to right: the tools, the zoomed picture, and the side (the 256
# colours, the recent ones, the colour in use, and under them the tiles of a
# sheet or the picture at its real size). The sizes follow the window: the
# colour cells get as large as the height leaves room for, the zoom as large
# as the middle allows. Every drawing here is only that part and does not
# present; the caller presents once.
class SpriteEditorApp < FmrbApp
  MENU_H = 15
  ST_H = 14
  TOOL_W = 40
  BTN_H = 18
  ITEM_H = 15
  LIST_ARROW_W = 10
  CHECK_A = 0xFF         # the transparent colour's checkerboard
  CHECK_B = 0xB6
  GRID = 0x92
  TOOLS = [:pen, :eraser, :fill, :pick, :undo_btn]
  FILE_ITEMS = [:new, :open, :save, :save_as, :close]
  EDIT_ITEMS = [:undo, :clear]
  HOTKEYS = { new: "Ctrl+N", open: "Ctrl+O", save: "Ctrl+S", undo: "Ctrl+Z" }

  # ------------------------------------------------------------ layout

  def layout
    @aw = @user_area_width
    @ah = @user_area_height
    @st_y = @ah - ST_H
    @body_y = MENU_H + 3
    @body_h = @st_y - 2 - @body_y
    # the colour cells: the largest that leaves the list its room
    list_min = @sheet ? TILE + 4 : @ih
    room_w = @aw - TOOL_W - 110
    pc = 10
    while pc > 4 && (18 * pc + 24 + list_min > @body_h || 16 * pc > room_w)
      pc -= 1
    end
    @pc = pc
    @rs = 2 * pc
    @side_w = 16 * pc
    @side_x = @aw - @side_w - 3
    @pal_x = @side_x
    @pal_y = @body_y
    @rec_y = @pal_y + 16 * pc + 4
    @cur_y = @rec_y + @rs + 3
    @list_x = @side_x
    @list_y = @cur_y + 13 + 4
    @list_w = @side_w
    @list_h = @st_y - 2 - @list_y
    # the zoomed picture
    cx0 = TOOL_W + 4
    cw = @side_x - 4 - cx0
    ch = @body_h
    z = cw / @rw
    z2 = ch / @rh
    z = z2 if z2 < z
    z = 20 if z > 20
    z = 1 if z < 1
    @z = z
    @grid = z >= 5 ? 1 : 0
    @cx = cx0 + (cw - z * @rw) / 2
    @cy = @body_y + (ch - z * @rh) / 2
    # the tiles of a sheet
    @list_cols = 1
    @list_rows = 1
    @list_over = false
    if @sheet
      @list_cols = (@list_w + 2) / (TILE + 2)
      @list_rows = (@list_h + 2) / (TILE + 2)
      @list_cols = 1 if @list_cols < 1
      @list_rows = 1 if @list_rows < 1
      if total_list_rows > @list_rows
        @list_over = true
        @list_cols = (@list_w - LIST_ARROW_W + 2) / (TILE + 2)
        @list_cols = 1 if @list_cols < 1
      end
      keep_tile_shown
    end
    # what is pressed by name
    @hits.clear
    x = 2
    @file_x = x
    w = text_px(@w[:file]) + 10
    @hits.add(:m_file, x, 0, w, MENU_H)
    x += w + 2
    @edit_x = x
    w = text_px(@w[:edit]) + 10
    @hits.add(:m_edit, x, 0, w, MENU_H)
    i = 0
    while i < TOOLS.size
      @hits.add(TOOLS[i], 2, tool_y(i), TOOL_W - 2, BTN_H)
      i += 1
    end
    if @list_over
      ax = @list_x + @list_w - LIST_ARROW_W
      half = @list_h / 2
      @hits.add(:l_up, ax, @list_y, LIST_ARROW_W, half)
      @hits.add(:l_down, ax, @list_y + half, LIST_ARROW_W, @list_h - half)
    end
    build_images
    @ready = true
  end

  # ------------------------------------------------------------ images
  #
  # A command to the drawing side per rectangle is what a redraw costs, so
  # what is drawn often is made once on the drawing side and stamped with
  # draw_tile: the 256 colours, and strips of the transparent checkerboard
  # (a run of transparent pixels is one stamp, not three rectangles a
  # pixel). Made again when the layout changes.

  def build_images
    free_images
    @cimg = checker_strip(@rw, @z, @grid)
    @timg = checker_strip(@sheet ? TILE : @iw, @sheet ? 1 : preview_scale, 0)
    @pimg = SpriteImage.new(@gfx, width: 16 * @pc, height: 16 * @pc)
    @pimg.set_target
    c = 0
    while c < 256
      x = (c & 15) * @pc
      y = (c >> 4) * @pc
      if c == TRANSPARENT
        checker(x, y, @pc, @pc, 0)
      else
        @gfx.fill_rect(x, y, @pc, @pc, c)
      end
      c += 1
    end
    @pimg.reset_target
  end

  def free_images
    @cimg.destroy if @cimg
    @timg.destroy if @timg
    @pimg.destroy if @pimg
    @cimg = nil
    @timg = nil
    @pimg = nil
  end

  # n cells of cell x cell pixels in two rows. A cell large enough has the
  # 2x2 checkerboard inside (clear of a grid line g pixels wide at its left
  # and top); a small one is one grey, alternating, starting with CHECK_A in
  # the first row and CHECK_B in the second, so row (y & 1) fits row y.
  def checker_strip(n, cell, g)
    w = n * cell
    img = SpriteImage.new(@gfx, width: w, height: 2 * cell)
    img.set_target
    @gfx.fill_rect(0, 0, w, 2 * cell, CHECK_A)
    s = cell - g
    hs = s / 2
    r = 0
    while r < 2
      i = 0
      while i < n
        x = i * cell
        y = r * cell
        if s >= 4
          @gfx.fill_rect(x + g, y + g, hs, hs, CHECK_B)
          @gfx.fill_rect(x + g + hs, y + g + hs, s - hs, s - hs, CHECK_B)
        elsif ((i + r) & 1) == 1
          @gfx.fill_rect(x, y, cell, cell, CHECK_B)
        end
        i += 1
      end
      r += 1
    end
    img.reset_target
    img
  end

  # Transparent cells i...i+n of a strip's row (parity) at (dx, dy).
  def stamp_clear(img, cell, i, n, parity, dx, dy)
    @gfx.draw_tile(img.id, i * cell, (parity & 1) * cell, n * cell, cell, dst_x: dx, dst_y: dy)
  end

  def tool_y(i)
    y = @body_y + i * (BTN_H + 3)
    y += 6 if TOOLS[i] == :undo_btn
    y
  end

  def total_list_rows
    n = @tcols * @trows
    (n + @list_cols - 1) / @list_cols
  end

  def text_px(s)
    n = s.bytesize
    i = 0
    px = 0
    while i < n
      b = s.getbyte(i)
      if b < 0x80
        px += 6
      elsif b >= 0xC0
        px += 12
      end
      i += 1
    end
    px
  end

  # ------------------------------------------------------------ the whole

  def draw_all
    return unless @ready
    clear_user_area
    draw_menu_bar
    draw_tools
    draw_canvas
    draw_palette
    draw_recent
    draw_current
    draw_side
    draw_status
    draw_menu if @menu
    draw_dialog if @dialog
    @gfx.present
  end

  # ------------------------------------------------------------ menu bar

  def draw_menu_bar
    @gfx.fill_rect(0, 0, @aw, MENU_H, @bar_bg)
    menu_word(:m_file, @file_x, @w[:file], @menu == :m_file)
    menu_word(:m_edit, @edit_x, @w[:edit], @menu == :m_edit)
  end

  def menu_word(id, x, label, on)
    w = text_px(label) + 10
    fg = @bar_fg
    if on
      @gfx.fill_rect(x, 1, w, MENU_H - 2, @bar_fg)
      fg = @bar_bg
    end
    @gfx.draw_text(x + 5, 2, label, fg)
  end

  def menu_list
    @menu == :m_file ? FILE_ITEMS : EDIT_ITEMS
  end

  def menu_box
    items = menu_list
    w = 0
    i = 0
    while i < items.size
      tw = text_px(@w[items[i]])
      hk = HOTKEYS[items[i]]
      tw += 12 + text_px(hk) if hk
      w = tw if tw > w
      i += 1
    end
    w += 14
    x = @menu == :m_file ? @file_x : @edit_x
    x = @aw - w - 1 if x + w > @aw - 1
    x = 0 if x < 0
    [x, MENU_H, w, ITEM_H * items.size + 2]
  end

  def draw_menu
    b = menu_box
    items = menu_list
    @gfx.fill_rect(b[0], b[1], b[2], b[3], theme_bg)
    @gfx.draw_rect(b[0], b[1], b[2], b[3], theme_border)
    i = 0
    while i < items.size
      y = b[1] + 3 + i * ITEM_H
      @gfx.draw_text(b[0] + 7, y, @w[items[i]], theme_fg)
      hk = HOTKEYS[items[i]]
      @gfx.draw_text(b[0] + b[2] - 7 - text_px(hk), y, hk, theme_border) if hk
      if i > 0
        ly = b[1] + 1 + i * ITEM_H
        @gfx.draw_line(b[0] + 3, ly, b[0] + b[2] - 4, ly, theme_border)
      end
      i += 1
    end
  end

  def menu_item_at(x, y)
    b = menu_box
    return nil if x < b[0] || x >= b[0] + b[2] || y < b[1] + 1 || y >= b[1] + b[3] - 1
    i = (y - b[1] - 1) / ITEM_H
    items = menu_list
    i < items.size ? items[i] : nil
  end

  def open_menu(id)
    @menu = id
    draw_menu_bar
    draw_menu
    @gfx.present
  end

  # Closed by an item, a press elsewhere, a key or a resize.
  def close_menu
    @menu_was = @menu
    @menu = nil
    draw_all
  end

  def close_menu_quiet
    @menu_was = nil
    @menu = nil
  end

  # ------------------------------------------------------------ tools

  def draw_tools
    @gfx.fill_rect(0, @body_y, TOOL_W + 1, @body_h, theme_bg)
    i = 0
    while i < TOOLS.size
      id = TOOLS[i]
      on = id == @tool
      fill = on ? theme_accent : theme_bg
      fg = on ? theme_fg_light : theme_fg
      y = tool_y(i)
      @gfx.draw_button(2, y, TOOL_W - 2, BTN_H, @w[id], fill, fg)
      @gfx.draw_rect(2, y, TOOL_W - 2, BTN_H, theme_border) unless on
      i += 1
    end
  end

  # ------------------------------------------------------------ the picture

  # (x, y) of the region under the area's (ax, ay), or nil.
  def canvas_xy(ax, ay)
    return nil if ax < @cx || ay < @cy
    x = (ax - @cx) / @z
    y = (ay - @cy) / @z
    return nil if x >= @rw || y >= @rh
    [x, y]
  end

  # The same, outside the picture too (a stroke that leaves it).
  def canvas_xy_free(ax, ay)
    [(ax - @cx) / @z, (ay - @cy) / @z]
  end

  def draw_canvas
    z = @z
    @gfx.draw_rect(@cx - 1, @cy - 1, @rw * z + 2 + @grid, @rh * z + 2 + @grid, theme_border)
    y = 0
    while y < @rh
      base = (@ry + y) * @iw + @rx
      x = 0
      while x < @rw
        c = @pix.getbyte(base + x)
        x2 = x + 1
        while x2 < @rw && @pix.getbyte(base + x2) == c
          x2 += 1
        end
        if c == TRANSPARENT
          stamp_clear(@cimg, z, x, x2 - x, y, @cx + x * z, @cy + y * z)
        else
          @gfx.fill_rect(@cx + x * z, @cy + y * z, (x2 - x) * z, z, c)
        end
        x = x2
      end
      y += 1
    end
    if @grid == 1
      w = @rw * z
      h = @rh * z
      i = 0
      while i <= @rw
        @gfx.draw_line(@cx + i * z, @cy, @cx + i * z, @cy + h, GRID)
        i += 1
      end
      i = 0
      while i <= @rh
        @gfx.draw_line(@cx, @cy + i * z, @cx + w, @cy + i * z, GRID)
        i += 1
      end
    end
  end

  def draw_cell(x, y, c)
    g = @grid
    px = @cx + x * @z + g
    py = @cy + y * @z + g
    s = @z - g
    if c == TRANSPARENT
      @gfx.draw_tile(@cimg.id, x * @z + g, (y & 1) * @z + g, s, s, dst_x: px, dst_y: py)
    else
      @gfx.fill_rect(px, py, s, s, c)
    end
  end

  # The transparent colour: a 2x2 checkerboard in the cell, or (when the
  # cell is too small for that) one of the two greys by position.
  def checker(px, py, w, h, parity)
    if w >= 4 && h >= 4
      hw = w / 2
      hh = h / 2
      @gfx.fill_rect(px, py, w, h, CHECK_A)
      @gfx.fill_rect(px, py, hw, hh, CHECK_B)
      @gfx.fill_rect(px + hw, py + hh, w - hw, h - hh, CHECK_B)
    else
      @gfx.fill_rect(px, py, w, h, (parity & 1) == 0 ? CHECK_A : CHECK_B)
    end
  end

  # ------------------------------------------------------------ colours

  def draw_palette
    @gfx.draw_tile(@pimg.id, 0, 0, 16 * @pc, 16 * @pc, dst_x: @pal_x, dst_y: @pal_y)
    draw_pal_cell(@color)
  end

  def draw_pal_cell(c)
    sx = (c & 15) * @pc
    sy = (c >> 4) * @pc
    @gfx.draw_tile(@pimg.id, sx, sy, @pc, @pc, dst_x: @pal_x + sx, dst_y: @pal_y + sy)
    @gfx.draw_rect(@pal_x + sx, @pal_y + sy, @pc, @pc, contrast(c)) if c == @color
  end

  def contrast(c)
    return 0x00 if c == TRANSPARENT
    r = (c >> 5) & 7
    g = (c >> 2) & 7
    b = c & 3
    90 * r + 177 * g + 77 * b > 1050 ? 0x00 : 0xFF
  end

  def palette_at(x, y)
    return nil if x < @pal_x || y < @pal_y
    col = (x - @pal_x) / @pc
    row = (y - @pal_y) / @pc
    return nil if col > 15 || row > 15
    row * 16 + col
  end

  def draw_recent
    @gfx.fill_rect(@pal_x, @rec_y, @side_w, @rs, theme_bg)
    i = 0
    while i < @recent.size
      c = @recent[i]
      x = @pal_x + i * @rs
      if c == TRANSPARENT
        checker(x, @rec_y, @rs - 1, @rs, 0)
      else
        @gfx.fill_rect(x, @rec_y, @rs - 1, @rs, c)
      end
      i += 1
    end
    @gfx.draw_rect(@pal_x - 1, @rec_y - 1, @side_w + 1, @rs + 2, theme_border)
  end

  def recent_at(x, y)
    return nil if y < @rec_y || y >= @rec_y + @rs || x < @pal_x
    i = (x - @pal_x) / @rs
    i < @recent.size ? @recent[i] : nil
  end

  def draw_current
    @gfx.fill_rect(@pal_x, @cur_y, @side_w, 13, theme_bg)
    sw = 2 * @rs
    if @color == TRANSPARENT
      checker(@pal_x, @cur_y, sw, 13, 0)
    else
      @gfx.fill_rect(@pal_x, @cur_y, sw, 13, @color)
    end
    @gfx.draw_rect(@pal_x, @cur_y, sw, 13, theme_border)
    @gfx.draw_text(@pal_x + sw + 4, @cur_y + 1, color_text(@color), theme_fg)
  end

  def color_text(c)
    hex = c.to_s(16).upcase
    hex = "0" + hex if hex.size < 2
    c == TRANSPARENT ? "#{hex} #{@w[:transparent]}" : hex
  end

  # ------------------------------------------------------------ the side

  # The tiles of a sheet, or the picture at its real size.
  def draw_side
    @gfx.fill_rect(@list_x, @list_y, @list_w, @list_h, theme_bg)
    return if @list_h < 4
    if @sheet
      draw_tile_list
    else
      ps = preview_scale
      draw_thumb(0, 0, @iw, @ih, @list_x, @list_y, ps)
    end
  end

  def preview_scale
    @list_h >= 2 * @ih && @list_w >= 2 * @iw ? 2 : 1
  end

  def draw_tile_list
    n = @tcols * @trows
    first = @list_top * @list_cols
    j = 0
    while j < @list_cols * @list_rows && first + j < n
      k = first + j
      x = @list_x + (j % @list_cols) * (TILE + 2) + 1
      y = @list_y + (j / @list_cols) * (TILE + 2) + 1
      draw_thumb((k % @tcols) * TILE, (k / @tcols) * TILE, TILE, TILE, x, y, 1)
      @gfx.draw_rect(x - 1, y - 1, TILE + 2, TILE + 2, theme_accent) if k == @tile
      j += 1
    end
    if @list_over
      ax = @list_x + @list_w - LIST_ARROW_W
      half = @list_h / 2
      @gfx.draw_rect(ax, @list_y, LIST_ARROW_W, @list_h, theme_border)
      mx = ax + LIST_ARROW_W / 2
      @gfx.fill_triangle(mx - 3, @list_y + half / 2 + 2, mx + 3, @list_y + half / 2 + 2, mx, @list_y + half / 2 - 2, theme_fg)
      my = @list_y + half + (@list_h - half) / 2
      @gfx.fill_triangle(mx - 3, my - 2, mx + 3, my - 2, mx, my + 2, theme_fg)
    end
  end

  # Pixels (sx, sy, w, h) of the picture at (dx, dy), each s x s.
  def draw_thumb(sx, sy, w, h, dx, dy, s)
    y = 0
    while y < h && dy + y * s < @list_y + @list_h
      base = (sy + y) * @iw + sx
      x = 0
      while x < w
        c = @pix.getbyte(base + x)
        x2 = x + 1
        while x2 < w && @pix.getbyte(base + x2) == c
          x2 += 1
        end
        if c == TRANSPARENT
          stamp_clear(@timg, s, x, x2 - x, y, dx + x * s, dy + y * s)
        else
          @gfx.fill_rect(dx + x * s, dy + y * s, (x2 - x) * s, s, c)
        end
        x = x2
      end
      y += 1
    end
  end

  # One pixel of the region changed: the same pixel on the side.
  def draw_thumb_px(x, y, c)
    if c == TRANSPARENT
      col = ((x + y) & 1) == 0 ? CHECK_A : CHECK_B
    else
      col = c
    end
    if @sheet
      j = @tile - @list_top * @list_cols
      return if j < 0 || j >= @list_cols * @list_rows
      tx = @list_x + (j % @list_cols) * (TILE + 2) + 1
      ty = @list_y + (j / @list_cols) * (TILE + 2) + 1
      @gfx.fill_rect(tx + x, ty + y, 1, 1, col)
    else
      s = preview_scale
      py = @list_y + y * s
      return if py >= @list_y + @list_h
      @gfx.fill_rect(@list_x + x * s, py, s, s, col)
    end
  end

  def list_at(x, y)
    return nil unless @sheet
    return nil if x < @list_x || y < @list_y || y >= @list_y + @list_h
    col = (x - @list_x) / (TILE + 2)
    row = (y - @list_y) / (TILE + 2)
    return nil if col >= @list_cols || row >= @list_rows
    k = (@list_top + row) * @list_cols + col
    k < @tcols * @trows ? k : nil
  end

  def scroll_list(d)
    max = total_list_rows - @list_rows
    max = 0 if max < 0
    t = @list_top + d
    t = max if t > max
    t = 0 if t < 0
    return if t == @list_top
    @list_top = t
    draw_side
    @gfx.present
  end

  def keep_tile_shown
    row = @tile / @list_cols
    @list_top = row if row < @list_top
    @list_top = row - @list_rows + 1 if row >= @list_top + @list_rows
    @list_top = 0 if @list_top < 0
  end

  # ------------------------------------------------------------ status

  def draw_status
    @gfx.fill_rect(0, @st_y - 1, @aw, ST_H + 1, theme_bg)
    @gfx.draw_line(0, @st_y - 1, @aw, @st_y - 1, theme_border)
    room = @aw - resize_corner - 6
    if @message
      @gfx.draw_text(3, @st_y + 1, @gfx.fit_text(@message, room, ".."), theme_fg)
      return
    end
    name = @path ? File.basename(@path) : @w[:untitled]
    s = "#{name}#{@dirty ? " *" : ""}  #{@iw}x#{@ih}"
    s += "  #{@w[:tile]} #{@tile}" if @sheet
    s += "  (#{@hover_x},#{@hover_y})" if @hover_x >= 0
    s += "  #{@w[:color]} #{color_text(@color)}"
    @gfx.draw_text(3, @st_y + 1, @gfx.fit_text(s, room, ".."), theme_fg)
  end

  # ------------------------------------------------------------ dialogs

  def open_confirm
    close_menu_quiet
    @dialog = :confirm
    draw_dialog
    @gfx.present
  end

  def open_size_dialog
    @dialog = :size
    draw_dialog
    @gfx.present
  end

  def close_dialog
    @dialog = nil
    draw_all
  end

  def draw_dialog
    @dhits.clear
    w = 232
    w = @aw - 8 if w > @aw - 8
    h = 70
    x = (@aw - w) / 2
    y = (@ah - h) / 2
    @gfx.fill_rect(x, y, w, h, theme_bg)
    @gfx.draw_rect(x, y, w, h, theme_border)
    @gfx.draw_rect(x + 1, y + 1, w - 2, h - 2, theme_border)
    if @dialog == :size
      @gfx.draw_text(x + 8, y + 6, @w[:new_title], theme_fg)
      bx = x + 8
      by = y + 24
      i = 0
      while i < SIZES.size
        s = SIZES[i]
        dialog_button("s#{s}".to_sym, bx, by, 50, "#{s}x#{s}", true)
        bx += 54
        i += 1
      end
      cw = text_px(@w[:cancel]) + 14
      dialog_button(:d_cancel, x + w - cw - 8, y + h - 22, cw, @w[:cancel], false)
    else
      @gfx.draw_text(x + 8, y + 6, @w[:unsaved], theme_fg)
      @gfx.draw_text(x + 8, y + 22, @w[:ask_save], theme_fg)
      bx = x + 8
      by = y + h - 22
      sw = text_px(@w[:save]) + 14
      dialog_button(:d_save, bx, by, sw, @w[:save], true)
      bx += sw + 4
      dw = text_px(@w[:dont_save]) + 14
      dialog_button(:d_discard, bx, by, dw, @w[:dont_save], false)
      cw = text_px(@w[:cancel]) + 14
      dialog_button(:d_cancel, x + w - cw - 8, by, cw, @w[:cancel], false)
    end
  end

  def dialog_button(id, x, y, w, label, main)
    fill = main ? theme_accent : theme_bg
    fg = main ? theme_fg_light : theme_fg
    @gfx.draw_button(x, y, w, 16, label, fill, fg)
    @gfx.draw_rect(x, y, w, 16, theme_border) unless main
    @dhits.add(id, x, y, w, 16)
  end
end
