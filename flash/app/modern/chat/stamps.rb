# Chat (doc/asterism_chat C3): stamps, required by chat.app.rb after its
# class body. A stamp is a small PNG (at most the host's stamp_px square and
# stamp_bytes, hello says) named by the sha256 of its bytes
# (report/c3h_c4a.md).
#
# Where the pictures come from (the picker):
# - the stamp packs installed by the App Store: /app/usr/<id>/ whose
#   <id>.app.toml has stamp_pack = true, every *.png in it but <id>.png (the
#   store's picture of the pack);
# - the user's own pictures: /home/stamps/*.png (the sprite editor's place).
# The distribution image carries no stamp at all (plan decision 8b).
#
# Every picture is read and written again (BMP332.to_png, in C) before it
# is named or sent: a PNG drawn on a PC with an alpha channel then has 0x01
# (transparent) where it was see-through and the file is the small palette
# PNG. The sha256 is that of the rewritten bytes, so the same picture is the
# same stamp on every device.
#
# That PNG is kept under STAMP_CACHE/<sha>.png, both one's own and the ones
# fetched from the host (stamp_get), so a stamp travels once. Nothing of it
# goes under /home/stamps: the own list never shows the fetched ones. It is
# drawn from there scaled up to about SHOW_PX (16x16 twice, 8x8 four times,
# larger ones as they are) by FmrbGfx's picture store (draw_picture), which
# keeps the sprite images and lets the least recently drawn go. A file left
# by an older version of this app holds the picture already scaled up: its
# size then asks for no more scaling, so it shows as large as before.
class ChatApp < FmrbApp
  OWN_DIR = "/home/stamps"
  PACK_ROOT = "/app/usr"
  STAMP_CACHE = "/cache/app/chat/s"
  STAMP_PX = 48          # until hello says (stamp_px)
  STAMP_BYTES = 8192     # until hello says (stamp_bytes)
  SOURCE_MAX = 65536     # a picture file larger than this is not read at all
  SHOW_PX = 32
  TAB_H = 16
  PICK_ITEMS_MAX = 96    # pictures of one tab looked at
  HEXDIGITS = "0123456789abcdef"

  def stamps_reset
    @stamp_px = STAMP_PX
    @stamp_bytes = STAMP_BYTES
    @picking = false
    @pick_tabs = []       # [[title, dir], ...]: the own pictures first, then the packs
    @pick_tab = 0
    @pick_items = []      # the pictures' paths of the tab
    @pick_top = 0         # the first row shown
    @pick_cell = SHOW_PX + 6
    @prep = {}            # path => [sha, show w, show h, w, h] | [:big, w, h] | [:heavy, bytes] | [:bad]
    @st_size = {}         # sha => [show w, show h, scale] (its file is here)
    @st_gone = {}         # sha => true: the host does not have it
    @st_want = []         # shas to fetch, oldest first
    @stamps_drawn = 0     # stamp lines drawn by the last draw_talk
  end

  # ------------------------------------------------------------ the sha256

  # Digest::SHA256 (the browser build's gem) or mbedTLS (the boards, the
  # sim, the native program); nil when the build has neither.
  def sha_hex(bytes)
    if Object.const_defined?(:Digest) && ::Digest.const_defined?(:SHA256)
      return ::Digest::SHA256.hexdigest(bytes)
    end
    return nil unless Object.const_defined?(:MbedTLS) && ::MbedTLS.const_defined?(:Digest)
    d = ::MbedTLS::Digest.new(:sha256)
    d.update(bytes)
    raw = d.finish
    out = "".dup
    i = 0
    while i < raw.bytesize
      b = raw.getbyte(i)
      out << HEXDIGITS[b >> 4] << HEXDIGITS[b & 15]
      i += 1
    end
    out
  end

  # ------------------------------------------------------------ pictures

  # How many times a w x h picture is drawn larger (about SHOW_PX).
  def show_scale(w, h)
    m = w > h ? w : h
    k = SHOW_PX / (m < 1 ? 1 : m)
    k < 1 ? 1 : k
  end

  def show_path(sha)
    "#{STAMP_CACHE}/#{sha[0, 32]}.png"
  end

  def ensure_cache_dir
    return if @cache_dir_ok
    ["/cache/app", "/cache/app/chat", STAMP_CACHE].each do |d|
      FmrbFile.mkdir(d) unless FmrbFile.exist?(d)
    end
    @cache_dir_ok = true
  end

  # A picture (a path, or data: bytes) read and written again as it is
  # sent: [png, w, h], or a refusal [:big, w, h] / [:heavy, bytes] / [:bad].
  # Its size is looked at before it is decoded, and the pixels stay in C.
  def rewrite_picture(path, data = nil)
    if data
      r = ::BMP332.to_png(data: data, max_width: @stamp_px, max_height: @stamp_px,
                          max_bytes: @stamp_bytes, max_file_bytes: SOURCE_MAX)
    else
      r = ::BMP332.to_png(path, max_width: @stamp_px, max_height: @stamp_px,
                          max_bytes: @stamp_bytes, max_file_bytes: SOURCE_MAX)
    end
    e = r[:error]
    return [r[:png], r[:width], r[:height]] if e.nil?
    return [:big, r[:width], r[:height]] if e == :too_big
    return [:heavy, r[:bytes]] if e == :too_heavy || e == :file_too_large
    Log.info("chat: picture: #{e}")
    [:bad]
  end

  # The stamp's PNG kept (written when it is not there yet); its shown
  # size, or nil.
  def keep_shown(sha, png)
    path = show_path(sha)
    unless FmrbFile.exist?(path)
      ensure_cache_dir
      FmrbFile.write(path, png)
    end
    shown_size(sha)
  end

  # The size the kept file of sha is shown at, noted in @st_size; nil when
  # it is not a picture.
  def shown_size(sha)
    i = ::BMP332.info(show_path(sha))
    return nil if i.nil?
    w = i[:width]
    h = i[:height]
    k = show_scale(w, h)
    @st_size[sha] = [w * k, h * k, k]
  end

  # One picture of the picker made ready (named and its PNG kept): the
  # @prep entry.
  def prepare(path)
    p = @prep[path]
    return p if p
    r = rewrite_picture(path)
    if r[0].is_a?(String)
      sha = sha_hex(r[0])
      if sha.nil?
        p = [:bad]
        set_status("この端末ではスタンプを送れません (sha256 がありません)。")
      else
        sz = keep_shown(sha, r[0])
        p = sz ? [sha, sz[0], sz[1], r[1], r[2]] : [:bad]
      end
    else
      p = r
    end
    @prep[path] = p
    p
  end

  # The PNG of a picture as it is sent (rewritten), or nil when it no
  # longer is the one named sha.
  def stamp_bytes_of(path, sha)
    r = rewrite_picture(path)
    return nil unless r[0].is_a?(String)
    sha_hex(r[0]) == sha ? r[0] : nil
  end

  # ------------------------------------------------------------ the picker's lists

  def scan_tabs
    tabs = [["自分の絵", OWN_DIR]]
    names = Dir.exist?(PACK_ROOT) ? (Dir.children(PACK_ROOT) rescue []) : []
    names = names.sort
    i = 0
    while i < names.size
      id = names[i]
      i += 1
      dir = "#{PACK_ROOT}/#{id}"
      toml = "#{dir}/#{id}.app.toml"
      next unless FmrbFile.exist?(toml)
      conf = FmrbToml.load(toml) rescue nil
      next unless conf.is_a?(Hash) && conf["stamp_pack"] == true
      t = conf["app_screen_name_ja"].to_s
      t = conf["app_screen_name"].to_s if t.empty?
      t = id if t.empty?
      tabs << [t, dir]
    end
    @pick_tabs = tabs
    @pick_tab = 0 if @pick_tab >= tabs.size
    # an empty own list shows the first pack instead
    @pick_tab = 1 if @pick_tab == 0 && tabs.size > 1 && pictures_in(OWN_DIR, nil).empty?
  end

  # The *.png of dir by name (skip: the pack's own picture for the store).
  def pictures_in(dir, skip)
    return [] unless Dir.exist?(dir)
    names = (Dir.children(dir) rescue [])
    out = []
    i = 0
    while i < names.size
      n = names[i]
      i += 1
      next if n == skip
      next unless n.size > 4 && n[-4, 4].downcase == ".png"
      out << n
      break if out.size >= PICK_ITEMS_MAX
    end
    out = out.sort
    j = 0
    while j < out.size
      out[j] = "#{dir}/#{out[j]}"
      j += 1
    end
    out
  end

  def load_tab
    t = @pick_tabs[@pick_tab]
    dir = t ? t[1] : OWN_DIR
    skip = @pick_tab == 0 ? nil : "#{File.basename(dir)}.png"
    @pick_items = pictures_in(dir, skip)
    @pick_top = 0
    # the cell fits the largest picture of the tab
    big = 16
    i = 0
    while i < @pick_items.size
      p = prepare(@pick_items[i])
      i += 1
      next unless p[0].is_a?(String)
      big = p[1] if p[1] > big
      big = p[2] if p[2] > big
    end
    @pick_cell = big + 6
  end

  def open_picker
    return close_picker if @picking
    return set_status("先に部屋を選んでください。") unless @cur
    @picking = true
    scan_tabs
    load_tab
    if @pick_tabs.size == 1 && @pick_items.empty?
      set_status("")
    end
    draw_all
  end

  def close_picker
    return unless @picking
    @picking = false
    @pick_items = []
    @prep = {}
    draw_all
  end

  # ------------------------------------------------------------ sending

  # A picture of the picker chosen: posted (and put when the host asks).
  def pick(i)
    path = @pick_items[i]
    return unless path
    p = prepare(path)
    case p[0]
    when :big
      return set_status("この絵は大きすぎます (#{p[1]}x#{p[2]})。スタンプは #{@stamp_px}x#{@stamp_px} までです。")
    when :heavy
      return set_status("この絵はファイルが大きすぎます (#{(p[1] + 1023) / 1024} KB)。スタンプは #{@stamp_bytes / 1024} KB までです。")
    when :bad
      return set_status("この絵は読めません: #{File.basename(path)}")
    end
    return set_status("前の発言を送っています。少し待ってください。") if @out
    return show_face(:name) if @name.empty?
    @cid_seq += 1
    @out = [@cur, nil, "#{Machine.board_millis}-#{@cid_seq}", p[0], path]
    @out_busy = false
    @out_put = false
    @picking = false
    draw_all
    set_status(@hello ? "送っています…" : "チャットホストにつながっていません。つながったら送ります。")
  end

  # The host does not have the picture of the stamp on its way: put it.
  def put_stamp
    png = stamp_bytes_of(@out[4], @out[3])
    if png.nil?
      @out = nil
      @out_busy = false
      return set_status("その絵は変わったか、無くなりました。もう一度選んでください。")
    end
    start_call(:stamp_put, nil, { "sha" => @out[3], "png" => png })
  end

  def took_stamp_put(v)
    @out_busy = false
    e = error_of(v)
    if e || !v.is_a?(Hash)
      @out = nil
      set_status(message_of(v))
      Log.info("chat: stamp_put refused: #{e}")
      return
    end
    Log.info("chat: stamp_put #{v['sha'].to_s[0, 12]} #{v['bytes']} B new=#{v['new']}")
    @out_put = false   # posted again next
  end

  # ------------------------------------------------------------ receiving

  # A stamp of a message held: its kept file, or asked for.
  def want_stamp(sha)
    return if sha.nil? || @st_size[sha] || @st_gone[sha]
    return if FmrbFile.exist?(show_path(sha)) && shown_size(sha)
    @st_want << sha unless @st_want.index(sha)
  end

  # The next stamp to fetch, of the messages held now.
  def next_wanted
    while @st_want.size > 0
      sha = @st_want[0]
      return sha if @m_stamp.index(sha) && !@st_size[sha] && !@st_gone[sha]
      @st_want.shift
    end
    nil
  end

  def took_stamp_get(sha, v)
    @st_want.delete(sha)
    e = error_of(v)
    if e == "no_stamp"
      @st_gone[sha] = true
      Log.info("chat: stamp #{sha[0, 12]} gone")
    elsif e || !v.is_a?(Hash) || !v["png"].is_a?(String)
      # left out until the room is opened again
      @st_gone[sha] = true
      Log.info("chat: stamp_get refused: #{e}")
    else
      png = v["png"]
      if sha_hex(png) != sha
        @st_gone[sha] = true
        Log.info("chat: stamp #{sha[0, 12]} does not match its sha")
      else
        r = rewrite_picture(nil, png)
        if r[0].is_a?(String) && keep_shown(sha, r[0])
          Log.info("chat: stamp #{sha[0, 12]} #{png.bytesize} B #{r[1]}x#{r[2]} kept (#{png.class})")
        else
          @st_gone[sha] = true
        end
      end
    end
    bottom = at_bottom?
    rewrap
    @top = max_top if bottom
    draw_talk
  end

  # ------------------------------------------------------------ drawing

  # Rows a stamp takes below the line with its name (the usual size while
  # it is fetched, one row for the word when it could not be).
  def stamp_rows(sha)
    sz = @st_size[sha]
    h = sz ? sz[1] : (@st_gone[sha] ? LINE_H - 2 : SHOW_PX)
    (h + 2 + LINE_H - 1) / LINE_H
  end

  # Line li (1, 2, ...) of a stamp message: its band of the picture, or a
  # word while it is not here.
  def draw_stamp_line(sha, li, x, y)
    sz = @st_size[sha]
    if sz.nil?
      return unless li == 1
      word = @st_gone[sha] ? "(スタンプは取れませんでした)" : "(スタンプ)"
      @gfx.draw_text(x, y, word, theme_border)
      return
    end
    sy = (li - 1) * LINE_H
    h = sz[1] - sy
    return if h <= 0
    h = LINE_H if h > LINE_H
    w = sz[0]
    w = @area_w - 4 - x if x + w > @area_w - 4
    @stamps_drawn += 1
    @gfx.draw_picture(show_path(sha), x, y - sy, scale: sz[2], clip: [x, y, w, h])
  end

  # The picker, in the place of the talk.
  def draw_picker
    x0 = @px
    @gfx.fill_rect(x0, @ans_y, @pw, @ans_h, theme_bg)
    # the tabs
    x = x0 + 2
    i = 0
    while i < @pick_tabs.size
      t = head_fit(@pick_tabs[i][0], 96)
      w = text_px(t) + 10
      break if x + w > @area_w - 4
      if i == @pick_tab
        @gfx.fill_rect(x, @ans_y, w, TAB_H - 2, theme_accent)
        @gfx.draw_text(x + 5, @ans_y + 1, t, theme_fg_light)
      else
        @gfx.draw_rect(x, @ans_y, w, TAB_H - 2, theme_border)
        @gfx.draw_text(x + 5, @ans_y + 1, t, theme_fg)
      end
      @hits.add(:tab, x, @ans_y, w, TAB_H - 2, i)
      x += w + 3
      i += 1
    end
    gy = @ans_y + TAB_H
    gh = @ans_h - TAB_H
    if @pick_items.empty?
      draw_pick_hint(x0 + 3, gy + 2)
      return
    end
    c = @pick_cell
    cols = pick_cols
    rows = gh / c
    rows = 1 if rows < 1
    k = @pick_top * cols
    r = 0
    while r < rows && k < @pick_items.size
      col = 0
      while col < cols && k < @pick_items.size
        cx = x0 + 3 + col * c
        cy = gy + r * c
        draw_pick_cell(@pick_items[k], cx, cy, c)
        @hits.add(:cell, cx, cy, c, c, k)
        col += 1
        k += 1
      end
      r += 1
    end
    total = (@pick_items.size + cols - 1) / cols
    if total > rows
      bar_h = gh * rows / total
      bar_h = 6 if bar_h < 6
      mx = total - rows
      by = gy + (gh - bar_h) * (@pick_top > mx ? mx : @pick_top) / mx
      @gfx.fill_rect(@area_w - 3, by, 2, bar_h, theme_border)
    end
  end

  def pick_cols
    n = (@pw - 8) / @pick_cell
    n < 1 ? 1 : n
  end

  def pick_rows_total
    cols = pick_cols
    (@pick_items.size + cols - 1) / cols
  end

  def pick_scroll(d)
    rows = (@ans_h - TAB_H) / @pick_cell
    rows = 1 if rows < 1
    mx = pick_rows_total - rows
    mx = 0 if mx < 0
    t = @pick_top + d
    t = mx if t > mx
    t = 0 if t < 0
    return if t == @pick_top
    @pick_top = t
    draw_all
  end

  def draw_pick_cell(path, x, y, c)
    p = prepare(path)
    if p[0].is_a?(String)
      sz = @st_size[p[0]]
      @gfx.draw_picture(show_path(p[0]), x + (c - p[1]) / 2, y + (c - p[2]) / 2, scale: sz[2]) if sz
    else
      @gfx.draw_rect(x + 2, y + 2, c - 4, c - 4, theme_border)
      @gfx.draw_text(x + (c - 12) / 2, y + (c - 12) / 2, p[0] == :bad ? "?" : "大", theme_border)
    end
  end

  # What to do when a tab has no picture.
  def draw_pick_hint(x, y)
    if @pick_tabs.size == 1
      msg = "スタンプがまだありません。App Store で「スタンプのパック」を入れるか、スプライトエディタで描いた絵を /home/stamps/ に保存すると、ここに出ます。"
    elsif @pick_tab == 0
      msg = "スプライトエディタで描いて /home/stamps/ に保存した絵 (48x48 まで) が、ここに出ます。"
    else
      msg = "このパックには絵がありません。"
    end
    w = @pw - 8
    sp = wrap(msg, w, w)
    i = 0
    while i < sp.size && y + LINE_H <= @ans_y + @ans_h
      @gfx.draw_text(x, y, msg.byteslice(sp[i], sp[i + 1]).to_s, theme_fg)
      y += LINE_H
      i += 2
    end
  end
end
