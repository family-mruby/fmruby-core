# Image Viewer (doc/asterism_chat C3b): shows one PNG or BMP picture, the
# default app for both (config/associations.toml).
#
# The picture is not read here: FmrbGfx's picture store has it read on the
# display side (a PNG's transparent pixels become the transparent colour
# 0x01) and draws it, over the checkerboard where it is transparent. It is
# drawn as large as the window allows at a whole multiple; one larger than
# the window is shrunk by a whole factor (every k-th pixel), so the whole
# picture is always in view.
#
# The bar on top: Open (the desktop's file dialog), Open in the sprite editor
# (spawned with the file), and < > for the previous and next picture of the
# same folder (also the Left and Right keys).
class ImageViewerApp < FmrbApp
  BAR_H = 15
  ST_H = 14
  ZOOM_MAX = 16
  EDITOR = "/app/tool/sprite_editor.app.rb"
  SC_O = 0x12

  WORDS_JA = {
    open: "開く", edit: "スプライトエディタで開く",
    none: "絵がありません。「開く」で PNG か BMP を選んでください。",
    open_failed: "開けません", too_big: "大きすぎて開けません",
  }
  WORDS_EN = {
    open: "Open", edit: "Open in Sprite Editor",
    none: "No picture. Choose a PNG or a BMP with Open.",
    open_failed: "Cannot open", too_big: "Too big to open",
  }

  # ------------------------------------------------------------ lifecycle

  def on_create
    @gfx.set_font(:ja, 12)
    @w = FmrbApp.language == "ja" ? WORDS_JA : WORDS_EN
    @hits = FmrbUI::HitAreas.new
    @bar_bg = FmrbColors.shade(FmrbConst::THEME_MENU_BG, theme_bg)
    @bar_fg = theme_fg_light
    @path = nil
    @iw = 0
    @ih = 0
    @message = nil
    layout
    draw_all
  end

  def on_update
    1000
  end

  def on_resize(new_width, new_height)
    layout
    draw_all
  end

  def on_resume
    draw_all
  end

  # A file from the File Manager (spawned with it), or the file dialog's
  # answer to Open.
  def on_control(msg)
    return unless msg["cmd"] == "file_selected"
    path = msg["path"]
    return unless path.is_a?(String) && path.size > 0
    load_picture(path)
  end

  # ------------------------------------------------------------ the picture

  # True when the picture is open; otherwise the status line says why and
  # the picture shown stays.
  def load_picture(path)
    # into the store without drawing anything (an empty clip)
    unless @gfx.draw_picture(path, 0, 0, clip: [0, 0, 0, 0])
      why = @gfx.picture_error
      Log.error("Viewer: cannot open #{path}: #{why}")
      sz = why == :too_big ? @gfx.picture_size(path) : nil
      if sz
        show_message("#{@w[:too_big]}: #{File.basename(path)} #{sz[0]}x#{sz[1]}")
      else
        show_message("#{@w[:open_failed]}: #{File.basename(path)}")
      end
      return false
    end
    sz = @gfx.picture_size(path)
    @gfx.release_picture(@path) if @path && @path != path
    @iw = sz[0]
    @ih = sz[1]
    @path = path
    @message = nil
    Log.info("Viewer: opened #{path} #{@iw}x#{@ih}")
    layout
    draw_all
    true
  end

  # The pictures of the picture's folder (by name), the one before (-1) or
  # after (1) it, round at the ends. One that cannot be opened is passed
  # over (its reason stays on the status line while the next is tried).
  def step_picture(dir)
    return unless @path
    folder = File.dirname(@path)
    names = []
    begin
      d = Dir.open(folder)
      while (ent = d.read)
        names << ent if picture_name?(ent)
      end
      d.close
    rescue => e
      Log.error("Viewer: cannot list #{folder}: #{e.message}")
      return
    end
    return if names.size < 2
    names = names.sort
    i = names.index(File.basename(@path)) || 0
    tries = names.size - 1
    while tries > 0
      i = (i + dir) % names.size
      return if load_picture(folder == "/" ? "/#{names[i]}" : "#{folder}/#{names[i]}")
      tries -= 1
    end
  end

  def picture_name?(name)
    low = name.downcase
    low.end_with?(".png") || low.end_with?(".bmp")
  end

  # ------------------------------------------------------------ layout

  def layout
    @aw = @user_area_width
    @ah = @user_area_height
    @st_y = @ah - ST_H
    @body_y = BAR_H + 2
    @body_h = @st_y - 2 - @body_y
    vw = @aw - 4
    vh = @body_h
    @zoom = 1
    @shrink = 1
    if @path
      z = vw / @iw
      z2 = vh / @ih
      z = z2 if z2 < z
      if z >= 1
        @zoom = z > ZOOM_MAX ? ZOOM_MAX : z
      else
        k = (@iw + vw - 1) / vw
        k2 = (@ih + vh - 1) / vh
        @shrink = k2 > k ? k2 : k
      end
    end
    @dw = (@iw + @shrink - 1) / @shrink
    @dh = (@ih + @shrink - 1) / @shrink
    @px = 2 + (vw - @dw * @zoom) / 2
    @py = @body_y + (vh - @dh * @zoom) / 2
    # the bar: what is pressed by name
    @hits.clear
    x = 2
    w = text_px(@w[:open]) + 10
    @hits.add(:open, x, 0, w, BAR_H)
    @open_x = x
    x += w + 2
    w = text_px(@w[:edit]) + 10
    @hits.add(:edit, x, 0, w, BAR_H)
    @edit_x = x
    @next_x = @aw - 2 - 16
    @prev_x = @next_x - 2 - 16
    @hits.add(:prev, @prev_x, 0, 16, BAR_H)
    @hits.add(:next, @next_x, 0, 16, BAR_H)
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

  # ------------------------------------------------------------ drawing

  def draw_all
    clear_user_area
    draw_bar
    draw_picture
    draw_status
    @gfx.present
  end

  def draw_bar
    @gfx.fill_rect(0, 0, @aw, BAR_H, @bar_bg)
    has = !@path.nil?
    bar_word(@open_x, @w[:open], true)
    bar_word(@edit_x, @w[:edit], has)
    bar_word(@prev_x + 3, "<", has)
    bar_word(@next_x + 3, ">", has)
  end

  def bar_word(x, label, on)
    @gfx.draw_text(x + 5, 2, label, on ? @bar_fg : theme_border)
  end

  def draw_picture
    unless @path
      @gfx.draw_text_lines(6, @body_y + 6, @w[:none], @aw - 12, theme_fg, 14)
      return
    end
    @gfx.draw_picture(@path, @px, @py, scale: @shrink > 1 ? -@shrink : @zoom, checker: true)
  end

  def draw_status
    @gfx.fill_rect(0, @st_y - 1, @aw, ST_H + 1, theme_bg)
    @gfx.draw_line(0, @st_y - 1, @aw, @st_y - 1, theme_border)
    room = @aw - resize_corner - 6
    s = @message
    if s.nil? && @path
      s = "#{File.basename(@path)}  #{@iw}x#{@ih}  " + (@shrink > 1 ? "1/#{@shrink}" : "x#{@zoom}")
    end
    @gfx.draw_text(3, @st_y + 1, @gfx.fit_text(s, room, ".."), theme_fg) if s
  end

  def show_message(s)
    @message = s
    draw_status
    @gfx.present
  end

  # ------------------------------------------------------------ actions

  def act(id)
    case id
    when :open
      dir = @path ? File.dirname(@path) : "/home"
      request_file_select("open", dir: dir)
    when :edit then open_in_editor
    when :prev then step_picture(-1)
    when :next then step_picture(1)
    end
  end

  # The kernel starts the sprite editor and hands it the file, as the File
  # Manager does.
  def open_in_editor
    return unless @path
    Log.info("Viewer: #{@path} to the sprite editor")
    send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
      {"cmd" => "spawn", "app_name" => EDITOR, "open_path" => @path})
  end

  # ------------------------------------------------------------ input

  def on_event(ev)
    case ev[:type]
    when :mouse_down
      return unless ev[:button] == 1
      id = @hits.id_at(ev[:x], ev[:y])
      act(id) if id
    when :key_down
      sc = ev[:scancode].to_i
      if ev_ctrl?(ev)
        act(:open) if sc == SC_O
        return
      end
      case sc
      when FmrbConst::KEY_LEFT then act(:prev)
      when FmrbConst::KEY_RIGHT then act(:next)
      else
        ch = ev[:character].to_i
        act(:edit) if ch == 101 || ch == 69    # e / E
      end
    end
  end
end
