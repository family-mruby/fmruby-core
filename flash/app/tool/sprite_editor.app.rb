# Sprite Editor (doc/asterism_chat C3a): draws RGB332 pictures and the
# 16x16 tiles of a tile sheet.
#
# One picture is open at a time, held as a String of RGB332 bytes (top-down,
# getbyte/setbyte; never an Array, which costs a word per pixel). A picture
# no larger than SINGLE_MAX on both sides is edited whole; a larger one is a
# sheet of TILE x TILE tiles, and the tile chosen in the list on the right is
# what the zoomed view edits. 0x01 is the transparent colour (as for sprites
# and the chat's stamps); it is never drawn as itself, since the window's key
# colour would let the desktop through, but as a checkerboard.
#
# Files go the way of the text editor: the File menu has New (the size is
# asked), Open and Save as through the desktop's file dialog
# (request_file_select), Save writes back to the file in the format it came
# in (BMP332.save goes by the extension). The dialog opens in STAMP_DIR (made
# when missing), and Save of a picture that was never saved is Save as.
# Unsaved changes are asked about
# before New, Open, a file handed over by the File Manager, the close button
# and Ctrl+Q.
#
# The class is in two files: this one (the picture, the tools, the files and
# the input) and sprite_editor/screen.rb (the layout and the drawing),
# required at the end.
class SpriteEditorApp < FmrbApp
  TRANSPARENT = 0x01
  SIZES = [16, 24, 32, 48]
  NEW_SIZE = 32          # the picture a start from the launcher opens with
  TILE = 16
  SINGLE_MAX = 48        # edited whole up to this on both sides
  UNDO_MAX = 10
  RECENT_MAX = 8
  STAMP_DIR = "/home/stamps"

  # Ctrl + these scancodes
  SC_N = 0x11
  SC_O = 0x12
  SC_S = 0x16
  SC_Z = 0x1D

  WORDS_JA = {
    file: "ファイル", edit: "編集",
    new: "新規", open: "開く", save: "保存", save_as: "名前を付けて保存", close: "閉じる",
    undo: "取り消し", clear: "全部を透明に",
    pen: "ペン", eraser: "消す", fill: "塗る", pick: "拾う", undo_btn: "戻す",
    color: "色", transparent: "透明", tile: "タイル", untitled: "(新しい絵)",
    new_title: "新しい絵の大きさ", cancel: "キャンセル",
    unsaved: "保存していない変更があります。", ask_save: "保存しますか?",
    dont_save: "保存しない",
    saved: "保存しました", save_failed: "保存できません", open_failed: "開けません",
    too_big: "大きすぎて開けません", not_tiles: "16 の倍数の大きさでないので開けません",
    nothing_to_undo: "取り消すものがありません",
  }
  WORDS_EN = {
    file: "File", edit: "Edit",
    new: "New", open: "Open", save: "Save", save_as: "Save as", close: "Close",
    undo: "Undo", clear: "Clear to transparent",
    pen: "Pen", eraser: "Erase", fill: "Fill", pick: "Pick", undo_btn: "Undo",
    color: "Color", transparent: "clear", tile: "Tile", untitled: "(new)",
    new_title: "Size of the new picture", cancel: "Cancel",
    unsaved: "There are unsaved changes.", ask_save: "Save them?",
    dont_save: "Don't save",
    saved: "Saved", save_failed: "Cannot save", open_failed: "Cannot open",
    too_big: "Too big to open", not_tiles: "Not a multiple of 16; cannot open",
    nothing_to_undo: "Nothing to undo",
  }
  MAX_PIXELS = 65536     # a sheet larger than this is refused (VM pool)

  # ------------------------------------------------------------ lifecycle

  def on_create
    @gfx.set_font(:ja, 12)
    @w = FmrbApp.language == "ja" ? WORDS_JA : WORDS_EN
    @hits = FmrbUI::HitAreas.new
    @dhits = FmrbUI::HitAreas.new
    @bar_bg = FmrbColors.shade(FmrbConst::THEME_MENU_BG, theme_bg)
    @bar_fg = theme_fg_light
    @tool = :pen
    @color = 0x00
    @recent = [0x00, 0xFF, 0xE0, 0x1C, 0x03, 0xFC, 0xE3, 0x1F]
    @menu = nil
    @dialog = nil
    @pending = nil
    @pending_path = nil
    @select_for = nil     # :open or :save_as while the file dialog is up
    @after_save = nil     # what a Save as asked for by a question goes on to
    @drawing = false
    @last_px = -1
    @last_py = -1
    @hover_x = -1
    @hover_y = -1
    @message = nil
    @list_top = 0
    @ready = false
    new_picture(NEW_SIZE)
    layout
    draw_all
  end

  def on_update
    500
  end

  def on_resize(new_width, new_height)
    close_menu_quiet
    layout
    draw_all
  end

  def on_resume
    draw_all if @ready
  end

  # A file from the File Manager (spawned with it), or what the file dialog
  # answered for Open / Save as.
  def on_control(msg)
    return unless msg["cmd"] == "file_selected"
    path = msg["path"]
    want = @select_for
    @select_for = nil
    after = @after_save
    @after_save = nil
    return unless path.is_a?(String) && path.size > 0
    if want == :save_as
      go_on(after) if save_to(with_extension(path)) && after
    elsif @dirty
      @pending = :load
      @pending_path = path
      open_confirm
    else
      load_picture(path)
    end
  end

  # Ctrl+Q (and the kernel's kill of a built-in): ask first when there is
  # something unsaved.
  def on_quit_request
    if @dirty
      @pending = :close
      open_confirm
    else
      stop
    end
  end

  # Ctrl+Q (and the Shell's kill) reach an app started by its path as a
  # plain "stop", not as quit_request: ask about unsaved changes there too.
  # Everything else (clear_and_stop included) goes to the base as it is.
  def _handle_system_control(msg)
    if msg["cmd"] == "stop" && @dirty
      on_quit_request
      return
    end
    super(msg)
  end

  # The close button: the base stops the app on its release. With unsaved
  # changes the release asks instead (the press still shows on the button).
  def _frame_event(ev)
    if @dirty && !@_frameless && !@_fullscreen && @_close_btn_pressed &&
       ev[:type] == :mouse_up && ev[:button] == 1
      cx = @window_width - CLOSE_BTN_CX_OFFSET
      if (ev[:wx] - cx).abs <= CLOSE_BTN_HIT_R && (ev[:wy] - CLOSE_BTN_CY).abs <= CLOSE_BTN_HIT_R
        @_close_btn_pressed = false
        @pending = :close
        open_confirm
        return
      end
    end
    super(ev)
  end

  # ------------------------------------------------------------ the picture

  def new_picture(size)
    @iw = size
    @ih = size
    @pix = "\x01" * (size * size)
    @path = nil
    @fmt = "png"
    set_mode_for_size
    @dirty = false
    @undo = []
  end

  def set_mode_for_size
    @sheet = @iw > SINGLE_MAX || @ih > SINGLE_MAX
    @tcols = @sheet ? @iw / TILE : 1
    @trows = @sheet ? @ih / TILE : 1
    @tile = 0
    @list_top = 0
    select_region
  end

  # The part of the picture the zoomed view edits.
  def select_region
    if @sheet
      @rx = (@tile % @tcols) * TILE
      @ry = (@tile / @tcols) * TILE
      @rw = TILE
      @rh = TILE
    else
      @rx = 0
      @ry = 0
      @rw = @iw
      @rh = @ih
    end
  end

  def load_picture(path)
    bmp = nil
    begin
      bmp = BMP332.load(path)
    rescue => e
      Log.error("SpriteEd: cannot open #{path}: #{e.message}")
      show_message("#{@w[:open_failed]}: #{File.basename(path)}")
      return
    end
    w = bmp[:width]
    h = bmp[:height]
    if w * h > MAX_PIXELS
      show_message(@w[:too_big])
      return
    end
    if (w > SINGLE_MAX || h > SINGLE_MAX) && (w % TILE != 0 || h % TILE != 0)
      show_message(@w[:not_tiles])
      return
    end
    @iw = w
    @ih = h
    @pix = bmp[:pixels]
    bmp = nil
    @path = path
    @fmt = BMP332.png_path?(path) ? "png" : "bmp"
    set_mode_for_size
    @dirty = false
    @undo = []
    @message = nil
    Log.info("SpriteEd: opened #{path} #{@iw}x#{@ih}#{@sheet ? " (#{@tcols}x#{@trows} tiles)" : ""}")
    layout
    draw_all
  end

  # True when saved now. A picture with no file yet asks for a name first
  # (false here); after_save is what to go on to once it is saved there.
  def save_current(after_save = nil)
    return save_to(@path) if @path
    ask_file(:save_as)
    @after_save = after_save
    false
  end

  # The desktop's file dialog, opened in STAMP_DIR (made when missing).
  def ask_file(what)
    begin
      Dir.mkdir(STAMP_DIR) unless Dir.exist?(STAMP_DIR)
    rescue => e
      Log.error("SpriteEd: cannot make #{STAMP_DIR}: #{e.message}")
    end
    @select_for = what
    @after_save = nil
    request_file_select(what == :open ? "open" : "save", dir: STAMP_DIR)
  end

  def save_to(path)
    begin
      BMP332.save(path, @iw, @ih, @pix)
    rescue => e
      Log.error("SpriteEd: cannot save #{path}: #{e.message}")
      show_message("#{@w[:save_failed]}: #{File.basename(path)}")
      return false
    end
    @path = path
    @fmt = BMP332.png_path?(path) ? "png" : "bmp"
    @dirty = false
    Log.info("SpriteEd: saved #{path}")
    show_message("#{@w[:saved]}: #{path}")
    true
  end

  # A name typed without a known extension gets .png (the stamps' format).
  def with_extension(path)
    low = path.downcase
    return path if low.end_with?(".png") || low.end_with?(".bmp")
    path + ".png"
  end

  def px_at(x, y)
    @pix.getbyte((@ry + y) * @iw + @rx + x)
  end

  # Sets one pixel of the region and draws it (no present).
  def put_px(x, y, c)
    i = (@ry + y) * @iw + @rx + x
    return if @pix.getbyte(i) == c
    @pix.setbyte(i, c)
    @dirty = true
    draw_cell(x, y, c)
    draw_thumb_px(x, y, c)
  end

  # ------------------------------------------------------------ undo

  def push_undo
    snap = "\x00" * (@rw * @rh)
    y = 0
    while y < @rh
      base = (@ry + y) * @iw + @rx
      x = 0
      while x < @rw
        snap.setbyte(y * @rw + x, @pix.getbyte(base + x))
        x += 1
      end
      y += 1
    end
    @undo << [@rx, @ry, @rw, @rh, snap]
    @undo.shift if @undo.size > UNDO_MAX
  end

  def undo
    if @undo.empty?
      show_message(@w[:nothing_to_undo])
      return
    end
    u = @undo.pop
    rx = u[0]
    ry = u[1]
    rw = u[2]
    rh = u[3]
    snap = u[4]
    y = 0
    while y < rh
      base = (ry + y) * @iw + rx
      x = 0
      while x < rw
        @pix.setbyte(base + x, snap.getbyte(y * rw + x))
        x += 1
      end
      y += 1
    end
    @dirty = true
    # back to the tile the step was in
    if @sheet && (rx != @rx || ry != @ry)
      @tile = (ry / TILE) * @tcols + rx / TILE
      select_region
      keep_tile_shown
    end
    draw_canvas
    draw_side
    draw_status
    @gfx.present
  end

  # ------------------------------------------------------------ the tools

  def stroke_start(x, y)
    case @tool
    when :pick
      c = px_at(x, y)
      set_color(c)
      @tool = :pen
      draw_tools
      draw_status
    when :fill
      push_undo
      flood(x, y, @color)
      add_recent(@color)
      draw_canvas
      draw_side
      draw_status
    else
      push_undo
      @drawing = true
      @last_px = x
      @last_py = y
      put_px(x, y, stroke_color)
      draw_status
    end
    @gfx.present
  end

  def stroke_color
    @tool == :eraser ? TRANSPARENT : @color
  end

  # The pointer moved to (x, y) of the region while the button is held: a
  # line from the last point, so a quick move leaves no gaps.
  def stroke_to(x, y)
    return if x == @last_px && y == @last_py
    c = stroke_color
    x0 = @last_px
    y0 = @last_py
    dx = (x - x0).abs
    dy = -(y - y0).abs
    sx = x0 < x ? 1 : -1
    sy = y0 < y ? 1 : -1
    err = dx + dy
    while true
      put_px(x0, y0, c) if x0 >= 0 && y0 >= 0 && x0 < @rw && y0 < @rh
      break if x0 == x && y0 == y
      e2 = 2 * err
      if e2 >= dy
        err += dy
        x0 += sx
      end
      if e2 <= dx
        err += dx
        y0 += sy
      end
    end
    @last_px = x
    @last_py = y
  end

  def stroke_end
    return unless @drawing
    @drawing = false
    add_recent(@color) if @tool == :pen
    draw_side
    draw_status
    @gfx.present
  end

  # The 4-connected area of the colour at (x, y), inside the region.
  def flood(x, y, c)
    from = px_at(x, y)
    return if from == c
    stack = [y * @rw + x]
    while stack.size > 0
      k = stack.pop
      px = k % @rw
      py = k / @rw
      i = (@ry + py) * @iw + @rx + px
      next if @pix.getbyte(i) != from
      @pix.setbyte(i, c)
      stack << k - 1 if px > 0
      stack << k + 1 if px < @rw - 1
      stack << k - @rw if py > 0
      stack << k + @rw if py < @rh - 1
    end
    @dirty = true
  end

  def clear_region
    push_undo
    y = 0
    while y < @rh
      base = (@ry + y) * @iw + @rx
      x = 0
      while x < @rw
        @pix.setbyte(base + x, TRANSPARENT)
        x += 1
      end
      y += 1
    end
    @dirty = true
    draw_canvas
    draw_side
    draw_status
    @gfx.present
  end

  def set_color(c)
    old = @color
    @color = c
    draw_pal_cell(old)
    draw_pal_cell(c)
    draw_current
  end

  def add_recent(c)
    i = @recent.index(c)
    @recent.delete_at(i) if i
    @recent.unshift(c)
    @recent.pop while @recent.size > RECENT_MAX
    draw_recent
  end

  def set_tool(t)
    @tool = t
    draw_tools
    @gfx.present
  end

  # ------------------------------------------------------------ actions

  def act(id)
    case id
    when :new then ask_first(:new)
    when :open then ask_first(:open)
    when :save
      save_current
    when :save_as then ask_file(:save_as)
    when :close then on_quit_request
    when :undo, :undo_btn then undo
    when :clear then clear_region
    when :pen, :eraser, :fill, :pick then set_tool(id)
    when :l_up then scroll_list(-1)
    when :l_down then scroll_list(1)
    end
  end

  # New, Open and Close go on at once when nothing is unsaved, and after
  # the question otherwise.
  def ask_first(what)
    if @dirty
      @pending = what
      open_confirm
    else
      go_on(what)
    end
  end

  def go_on(what)
    case what
    when :new then open_size_dialog
    when :open then ask_file(:open)
    when :load then load_picture(@pending_path)
    when :close then stop
    end
  end

  def show_message(s)
    @message = s
    if @ready
      draw_status
      @gfx.present
    end
  end

  # ------------------------------------------------------------ input

  def on_event(ev)
    t = ev[:type]
    if @dialog
      dialog_event(ev)
      return
    end
    if @menu
      if t == :mouse_down
        item = menu_item_at(ev[:x], ev[:y])
        bar = item ? nil : @hits.id_at(ev[:x], ev[:y])
        close_menu
        if item
          act(item)
        elsif (bar == :m_file || bar == :m_edit) && bar != @menu_was
          open_menu(bar)
        end
      elsif t == :key_down
        close_menu
      end
      return
    end
    case t
    when :mouse_down
      return unless ev[:button] == 1
      press(ev[:x], ev[:y])
    when :mouse_move
      move(ev[:x], ev[:y])
    when :mouse_up
      # moves are merged on the way here, so the release can be the first
      # word of where the pointer went last: the stroke goes there too
      if @drawing
        c = canvas_xy_free(ev[:x], ev[:y])
        stroke_to(c[0], c[1])
      end
      stroke_end
    when :mouse_wheel
      rows = wheel_rows(ev)
      scroll_list(-rows) if rows && @sheet
    when :key_down
      key(ev)
    end
  end

  def press(x, y)
    @message = nil
    c = canvas_xy(x, y)
    if c
      stroke_start(c[0], c[1])
      return
    end
    id = @hits.id_at(x, y)
    if id == :m_file || id == :m_edit
      open_menu(id)
      return
    end
    if id
      act(id)
      return
    end
    col = palette_at(x, y)
    col = recent_at(x, y) if col.nil?
    if col
      set_color(col)
      @tool = :pen if @tool == :eraser || @tool == :pick
      draw_tools
      draw_status
      @gfx.present
      return
    end
    k = list_at(x, y)
    if k && k != @tile
      @tile = k
      select_region
      draw_canvas
      draw_side
      draw_status
      @gfx.present
    end
  end

  def move(x, y)
    if @drawing
      c = canvas_xy_free(x, y)
      stroke_to(c[0], c[1])
      @hover_x = c[0]
      @hover_y = c[1]
      draw_status
      @gfx.present
      return
    end
    c = canvas_xy(x, y)
    hx = c ? c[0] : -1
    hy = c ? c[1] : -1
    return if hx == @hover_x && hy == @hover_y
    @hover_x = hx
    @hover_y = hy
    draw_status
    @gfx.present
  end

  def key(ev)
    sc = ev[:scancode].to_i
    if ev_ctrl?(ev)
      case sc
      when SC_Z then undo
      when SC_S then act(:save)
      when SC_O then act(:open)
      when SC_N then act(:new)
      end
      return
    end
    ch = ev[:character].to_i
    case ch
    when 112 then set_tool(:pen)      # p
    when 101 then set_tool(:eraser)   # e
    when 102 then set_tool(:fill)     # f
    when 105 then set_tool(:pick)     # i
    end
  end

  def dialog_event(ev)
    t = ev[:type]
    if t == :key_down
      if ev[:scancode].to_i == 0x29 || ev[:character].to_i == 27
        close_dialog
      end
      return
    end
    return unless t == :mouse_down && ev[:button] == 1
    id = @dhits.id_at(ev[:x], ev[:y])
    return if id.nil?
    kind = @dialog
    close_dialog
    if kind == :size
      new_size(id)
    else
      what = @pending
      @pending = nil
      if id == :d_save
        go_on(what) if save_current(what)
      elsif id == :d_discard
        @dirty = false
        go_on(what)
      end
      draw_status if @ready && @dialog.nil?
      @gfx.present
    end
  end

  def new_size(id)
    i = 0
    while i < SIZES.size
      if id == "s#{SIZES[i]}".to_sym
        new_picture(SIZES[i])
        @message = nil
        layout
        draw_all
        return
      end
      i += 1
    end
  end
end

# the layout and the drawing (one class, two files)
require "/app/tool/sprite_editor/screen"
