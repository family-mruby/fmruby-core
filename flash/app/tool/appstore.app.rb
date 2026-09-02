# The app store: the same Ruby on Retro, on Modern and in the browser.
#
# Fetching goes through FmrbNet.request, which is asked from on_update rather
# than waited on, so the browser (whose tasks are cooperative) keeps drawing
# while the page fetches. Apps land in /app/usr/<id>/, which is where the
# launcher's third-level scan looks and, in the browser, one of the two
# directories that survive a reload.
#
# Two faces, because a 320x240 window cannot hold two lists side by side:
# "find" is the published list, "installed" is what is on this machine.

class AppStoreApp < FmrbApp
  DEFAULT_BASE = "https://raw.githubusercontent.com/family-mruby/family-mruby-apps/main/"
  # Somewhere else to shop, for anyone trying a list before it is published
  # (and for checking that the digest actually refuses a bad one). One line
  # holding a URL that ends in a slash.
  BASE_FILE = "/home/appstore_base.txt"
  INSTALL_ROOT = "/app/usr"
  CACHE_PATH = "/var/cache/launcher_index"

  ROW_H = 11
  THUMB_W = 32
  THUMB_H = 24
  TAB_H = 16
  # Somewhere the display side can be handed a file. /cache is what the rest
  # of the system uses for exactly this.
  THUMB_DIR = "/cache/app/appstore"

  # What each machine gives an app, from fmrb_mem_config.h and the display
  # settings. The store has to know these to answer "does this fit here".
  ENV_INFO = {
    "retro"  => { w: 320, h: 240, pool: 500,  large: 1024 },
    "modern" => { w: 426, h: 240, pool: 1024, large: 2048 },
    "web"    => { w: 426, h: 240, pool: 1536, large: 3072 },
  }

  def base
    return @base if @base
    b = begin
      line = File.open(BASE_FILE, "r") { |f| f.read.strip }
      (line && !line.empty?) ? line : DEFAULT_BASE
    rescue
      DEFAULT_BASE
    end
    b = b + "/" unless b.end_with?("/")
    @base = b
  end

  # One picture, of whatever is selected, in a fixed place. Not one per row:
  # a 26 px row halves how many apps fit, every row costs a PNG decode on
  # redraw (about 75 ms on a board), and the display side holds only eight
  # images at once.
  #
  # Retro has none at all. Its display is on the far side of a UART and every
  # picture has to cross it before it can be drawn, which is not worth it to
  # look at a list.
  def pictures?
    @env != "retro"
  end

  def row_h
    ROW_H
  end

  def on_create
    @env = detect_env
    @info = ENV_INFO[@env] || ENV_INFO["modern"]
    @face = :find              # :find or :installed
    @apps = []
    @sel = 0
    @scroll = 0
    @note = "fetching the list..."
    @job = nil                 # an install in progress
    @thumbs = {}               # app id -> local path, or :none once given up
    @synced = {}               # paths already handed to the display side
    @images = {}               # app id -> a decoded image on the display side
    @image_order = []          # oldest first, for evicting
    @thumb_req = nil           # [id, request] -- one at a time
    @req = FmrbNet.request(base + "registry.tsv")
    @ui = FmrbUI.new(self)
    build_ui
    draw_screen
  end

  # The environment an app declares in app_env. HW_FAMILY stands in for a
  # family in the simulator and in the browser, so it cannot say "web" on its
  # own; BOARD is what does.
  def detect_env
    return "web" if ::FmrbConst::BOARD == "wasm"
    ::FmrbConst::HW_FAMILY
  end

  # ---- layout -------------------------------------------------------------

  def area_w
    @user_area_x1 - @user_area_x0
  end

  def area_h
    @user_area_y1 - @user_area_y0
  end

  # Tabs, then the buttons, then the list, then two lines about the selection.
  # The controls sit near the top on purpose: the bottom edge of a window this
  # small is where things get clipped, and a button that is half there is worse
  # than one that is lower down the reading order.
  # FmrbUI adds the user area's own origin to every widget it places, so its
  # coordinates are relative to the user area while everything drawn through
  # @gfx here is canvas-absolute. Passing absolute coordinates to FmrbUI puts
  # each widget one user area lower than intended -- which is what made the
  # buttons sit on the first row of the list, and what the "flush paints past
  # its widgets" note used to be about. (fmrb-ui.rb says the confirm dialog
  # once had the same eleven-pixel bug.)
  # All user-area relative, for FmrbUI. Two, not one: a widget draws its own
  # frame on its first row, so at 1 that frame lands against the title bar and
  # nothing shows between them. At 2 there is one row of background, which is
  # the pixel that was wanted. (Measured off a screenshot -- see the note on
  # coordinates below; guessing at spacing here has been wrong twice.)
  TABS_TOP = 2
  TAB_ROW_H = TAB_H - 2
  BAR_TOP = TABS_TOP + TAB_ROW_H + 4
  BAR_H = 16
  # One width for all of them, so the four at the top left are a block rather
  # than four sizes in a row. 66 is the longest label ("Installed", nine
  # characters at 6 px) with room around it.
  BTN_W = 66
  BTN_GAP = 4

  def bar_y
    @user_area_y0 + BAR_TOP
  end

  # A rule under the buttons as well as above the detail, so the window reads
  # as three bands: what to look at, what to do, and what is selected.
  def top_rule_y
    bar_y + BAR_H + 4
  end

  # Below the rule, with air on both sides of it. FmrbUI's flush paints a
  # little past its widgets, which is why the rule cannot sit any closer to
  # the buttons than this.
  def list_y
    top_rule_y + 5
  end

  def detail_y
    @user_area_y1 - 41
  end

  # A rule between the list and what is written about the selection, so the
  # two read as two things.
  def rule_y
    detail_y - 5
  end

  def list_h
    h = rule_y - list_y
    h < row_h ? row_h : h
  end

  def rows_visible
    n = list_h / row_h
    n < 1 ? 1 : n
  end

  # Relative to the user area -- see the note on TABS_TOP.
  def build_ui
    w = area_w
    # Which list to look at, on one row: the two faces and the button that
    # fetches the list again all answer the same question.
    col2 = BTN_W + BTN_GAP
    @ui.toggle(:f_find, 0, TABS_TOP, BTN_W, TAB_ROW_H, "Find", group: :face, on: true)
    @ui.toggle(:f_inst, col2, TABS_TOP, BTN_W, TAB_ROW_H, "Installed", group: :face)
    @ui.button(:b_refresh, w - BTN_W, TABS_TOP, BTN_W, TAB_ROW_H, "Reload")
    # What to do with the selected app, on the next row, under the two above
    # them. They used to sit at opposite ends of one row with Reload, which
    # read as three unrelated buttons.
    @ui.button(:b_go, 0, BAR_TOP, BTN_W, BAR_H, "Install")
    @ui.button(:b_del, col2, BAR_TOP, BTN_W, BAR_H, "Remove")
  end

  # ---- drawing ------------------------------------------------------------

  def draw_screen
    clear_user_area
    @ui.flush
    draw_list
    draw_rules
    draw_detail
    a = current
    draw_thumb(a)
    want_thumb(a)
    draw_window_frame
    @gfx.present
  end

  def shown
    return @apps if @face == :find
    list = []
    i = 0
    while i < @apps.size
      list << @apps[i] if installed_version(@apps[i]["id"])
      i += 1
    end
    list
  end

  # The picture of whatever is selected, drawn in the corner of the detail
  # area. One at a time: the display side holds eight images at once
  # (DISPLAY_P4_MAX_IMAGES), and a decode is about 75 ms on a board, so
  # create-draw-delete for one is affordable where one per row was not.
  def draw_thumb(a)
    return unless pictures?
    x = @user_area_x1 - THUMB_W - 2
    y = detail_y
    @gfx.fill_rect(x, y, THUMB_W, THUMB_H, theme_bg)
    path = a ? @thumbs[a["id"].to_s] : nil
    if path.nil? || path == :none
      @gfx.draw_rect(x, y, THUMB_W, THUMB_H, theme_border)
      return
    end
    begin
      img_id = image_for(a["id"].to_s, path)
      if img_id
        @gfx.draw_image(img_id, x: x, y: y)
      else
        @thumbs[a["id"].to_s] = :none
      end
    rescue => e
      Log.warn("appstore: thumb #{a['id']}: #{e.message}")
      @thumbs[a["id"].to_s] = :none
    end
  end

  # The display side keeps a decoded picture until it is told otherwise, and
  # decoding one costs about 75 ms on a board. Keeping the last few means
  # moving back to an app already looked at costs a draw and nothing else.
  #
  # Only a few: there are eight image slots for the whole machine
  # (DISPLAY_P4_MAX_IMAGES), and the other apps need some.
  IMAGE_CACHE_MAX = 4

  def image_for(id, path)
    cached = @images[id]
    return cached if cached

    # sync_file does not send a file that already matches, but the comparison
    # is itself a round trip to the display side. These files are written once
    # and never change, so remember what has been sent.
    unless @synced[path]
      @gfx.sync_file(path)
      @synced[path] = true
    end
    img = @gfx.create_image(path)
    return nil unless img
    # A slot that could not be had comes back as id 0, which is truthy in
    # Ruby and would be cached and drawn forever after.
    id_new = img[:id]
    return nil if id_new.nil? || id_new == 0

    while @image_order.size >= IMAGE_CACHE_MAX
      old_id = @image_order.shift
      old_img = @images[old_id]
      if old_img
        @gfx.delete_image(old_img)
        @images[old_id] = nil
      end
    end
    @images[id] = id_new
    @image_order << id
    id_new
  end

  # Ask for the selected app's picture, if it has one and we have not got it.
  # One at a time, and never while an install is using the network.
  def want_thumb(a)
    return unless pictures?
    return if a.nil?
    return if @job
    return if @thumb_req
    id = a["id"].to_s
    return if @thumbs.key?(id)
    path = a["thumb_path"]
    return if path.nil? || path.empty?
    @thumbs[id] = :none          # claimed, so the next redraw does not re-ask
    @thumb_req = [id, FmrbNet.request(base + a["base"].to_s + path)]
  end

  def step_thumb
    return false unless @thumb_req
    req = @thumb_req[1]
    return false unless req.done?
    id = @thumb_req[0]
    @thumb_req = nil
    return true unless req.ok?
    begin
      Dir.mkdir("/cache/app") unless Dir.exist?("/cache/app")
      Dir.mkdir(THUMB_DIR) unless Dir.exist?(THUMB_DIR)
      path = "#{THUMB_DIR}/#{id}.png"
      File.open(path, "w") { |f| f.write(req.body) }
      @thumbs[id] = path
    rescue => e
      Log.warn("appstore: could not keep #{id}.png: #{e.message}")
    end
    true
  end

  def draw_list
    items = shown
    x = @user_area_x0
    y = list_y
    w = area_w
    @gfx.fill_rect(x, y, w, list_h, theme_bg)
    if items.empty?
      @gfx.draw_text(x + 2, y + 2, @face == :find ? "(nothing yet)" : "(none installed)", theme_fg)
      return
    end
    @scroll = 0 if @scroll > items.size - 1
    i = 0
    n = rows_visible
    while i < n
      idx = @scroll + i
      break if idx >= items.size
      a = items[idx]
      ry = y + i * row_h
      # A marker rather than a highlight bar: a filled bar leaves nowhere for
      # the ink to be legible against both themes.
      draw_row(a, x, ry, idx == @sel)
      i += 1
    end
  end

  # One row. Kept apart from draw_list so that moving the selection can
  # repaint two rows instead of the whole window: on a board a full redraw is
  # 300-440 ms, and that wait is what the flicker was.
  # Two one-character columns before the name -- the cursor and the installed
  # mark -- so the names line up whether or not either is there. They used to
  # be a prefix of whatever length the marks happened to need, which moved
  # every name as the selection passed.
  def draw_row(a, x, ry, sel)
    fg = fits?(a) ? theme_fg : theme_border
    @gfx.fill_rect(x, ry, area_w, ROW_H, theme_bg)
    @gfx.draw_text(x + 2, ry + 1, sel ? ">" : " ", fg, theme_bg)
    @gfx.draw_text(x + 10, ry + 1, install_mark(a), fg, theme_bg)
    @gfx.draw_text(x + 20, ry + 1, row_label(a), fg, theme_bg)
  end

  # "=" installed and current, "^" a newer version is out, blank otherwise.
  def install_mark(a)
    have = installed_version(a["id"].to_s)
    return " " if have.nil?
    have == a["version"] ? "=" : "^"
  end

  def row_y(idx)
    list_y + (idx - @scroll) * ROW_H
  end

  def row_visible?(idx)
    idx >= @scroll && idx < @scroll + rows_visible
  end

  # Less the two marker columns and the margin.
  def row_cols
    n = (area_w - 22) / 6
    n < 4 ? 4 : n
  end

  def row_label(a)
    "#{a['name']} v#{a['version']}"[0, row_cols].to_s
  end

  def draw_rules
    x0 = @user_area_x0
    x1 = @user_area_x1 - 1
    @gfx.draw_line(x0, top_rule_y, x1, top_rule_y, theme_border)
    @gfx.draw_line(x0, rule_y + 2, x1, rule_y + 2, theme_border)
  end

  def draw_detail
    x = @user_area_x0
    y = detail_y
    w = area_w
    # Leave the corner alone: the selected app's picture lives there.
    w -= (THUMB_W + 4) if pictures?
    cols = w / 6
    @gfx.fill_rect(x, y, w, 40, theme_bg)
    a = current
    if a
      ver = a["version"].to_s
      have = installed_version(a["id"].to_s)
      head = have ? "v#{ver} (have #{have}) #{a['category']}" : "v#{ver} #{a['category']}"
      @gfx.draw_text(x + 2, y, head[0, cols].to_s, theme_fg, theme_bg)
      why = blocked_reason(a)
      @gfx.draw_text(x + 2, y + 10, (why || a["description"]).to_s[0, cols].to_s,
                     why ? theme_border : theme_fg, theme_bg)
    end
    # Two lines for the note: the useful ones are failures, and a failure
    # explains itself in more characters than a 280 px window holds.
    note = @note.to_s
    @gfx.draw_text(x + 2, y + 20, note[0, cols].to_s, theme_fg, theme_bg)
    rest = note.length > cols ? note[cols, cols].to_s : ""
    @gfx.draw_text(x + 2, y + 30, rest, theme_fg, theme_bg)
  end

  # ---- the list -----------------------------------------------------------

  def current
    items = shown
    return nil if items.empty?
    @sel = items.size - 1 if @sel >= items.size
    items[@sel]
  end

  def fits?(a)
    blocked_reason(a).nil?
  end

  # Every field arrives as a string from the list, and an empty one means
  # "not said" rather than zero.
  def num(a, key)
    v = a[key]
    return nil if v.nil?
    v = v.to_s
    return nil if v.empty?
    v.to_i
  end

  # nil when it can run here, otherwise the reason in one short line.
  def blocked_reason(a)
    envs = a["env"]
    if envs.is_a?(Array) && !envs.empty? && !envs.include?(@env)
      return "not checked on #{@env}"
    end
    mw = num(a, "min_width")
    return "needs #{mw}px wide" if mw && mw > @info[:w]
    mh = num(a, "min_height")
    return "needs #{mh}px tall" if mh && mh > @info[:h]
    kb = heap_kb(a)
    return "needs #{kb}KB heap" if kb && kb > @info[:large]
    nil
  end

  # The number for this machine's object model: boards are 32-bit, the
  # simulator and the browser share the linux pool sizes.
  def heap_kb(a)
    num(a, (@env == "web") ? "heap_linux" : "heap_esp32")
  end

  # ---- what is on this machine -------------------------------------------

  def app_dir(id)
    "#{INSTALL_ROOT}/#{id}"
  end

  # The installed .app.toml is the record: app_id and app_version are in it,
  # so there is no separate ledger to keep in step with the files.
  def installed_version(id)
    path = "#{app_dir(id)}/#{id}.app.toml"
    return nil unless File.exist?(path)
    text = File.open(path, "r") { |f| f.read }
    return "?" unless text
    value_of(text, "app_version") || "?"
  end

  # One key out of the flat toml the device itself reads. No Regexp on this
  # machine, so this walks the lines.
  def value_of(text, key)
    lines = text.split("\n")
    i = 0
    while i < lines.size
      line = lines[i].strip
      i += 1
      next if line.empty?
      next if line.start_with?("#")
      eq = line.index("=")
      next unless eq
      next unless line[0, eq].strip == key
      v = line[eq + 1, line.length - eq - 1].strip
      len = v.length
      v = v[1, len - 2] if len >= 2 && v[0] == "\"" && v[len - 1] == "\""
      return v
    end
    nil
  end

  # ---- installing ---------------------------------------------------------

  def start_install(a)
    why = blocked_reason(a)
    if why
      @note = why
      return
    end
    files = a["files"]
    unless files.is_a?(Array) && !files.empty?
      @note = "the list has no files for it"
      return
    end
    @job = { app: a, files: files, idx: 0, got: [], req: nil, digest: new_digest }
    @note = "getting 1/#{files.size}..."
  end

  HEX = "0123456789abcdef"

  # One digest for the whole app, not one per file: the list carries a single
  # sha256 and this keeps a single running digest against it, whatever the
  # app grows to. Knowing which file differed would not help -- a mismatch
  # throws the install away whole.
  #
  # nil where there is no digest to be had. The browser build carries no
  # mbedTLS (it has no sockets, so it has none of the gems that pull it in),
  # and saying so is better than pretending the check happened.
  def new_digest
    return nil unless Object.const_defined?(:MbedTLS)
    ::MbedTLS::Digest.new(:sha256)
  rescue => e
    Log.warn("appstore: no digest available: #{e.message}")
    nil
  end

  # Each file is framed by its own name and length before its bytes, so that
  # two different splits of the same stream cannot hash alike. tools/registry.rb
  # in family-mruby-apps builds it the same way, in the same order.
  def digest_file(d, name, body)
    return unless d
    d.update(name)
    d.update("\n")
    d.update(body.bytesize.to_s)
    d.update("\n")
    d.update(body)
  end

  def to_hex(bin)
    out = ""
    i = 0
    n = bin.bytesize
    while i < n
      b = bin.getbyte(i)
      out += HEX[b >> 4]
      out += HEX[b & 15]
      i += 1
    end
    out
  end

  # Driven from on_update. Everything is fetched before anything is written,
  # so a failure halfway through cannot leave half an app behind.
  def step_install
    job = @job
    return unless job
    if job[:req].nil?
      f = job[:files][job[:idx]]
      job[:req] = FmrbNet.request(base + job[:app]["base"].to_s + f["path"].to_s)
      return
    end
    return unless job[:req].done?
    req = job[:req]
    f = job[:files][job[:idx]]
    unless req.ok?
      @note = "#{f['path']}: #{req.error || "status #{req.status}"}"
      @job = nil
      return
    end
    body = req.body.to_s
    size = f["size"]
    # Size first: it is free, and a truncated transfer is worth catching
    # before the digest gets a chance to blame the whole app for it.
    if size.is_a?(Integer) && body.bytesize != size
      @note = "#{f['path']}: #{body.bytesize}B, expected #{size}B"
      @job = nil
      return
    end
    digest_file(job[:digest], f["path"].to_s, body)
    job[:got] << [f["path"].to_s, body]
    job[:req] = nil
    job[:idx] += 1
    if job[:idx] >= job[:files].size
      return unless digest_ok?(job)
      finish_install(job)
    else
      @note = "getting #{job[:idx] + 1}/#{job[:files].size}..."
    end
  end

  # true to go on, false when the install has already been abandoned.
  def digest_ok?(job)
    want = job[:app]["sha256"]
    d = job[:digest]
    if d.nil? || !want.is_a?(String)
      # No check was made, and the note says so rather than implying one was.
      job[:unchecked] = true
      return true
    end
    got = to_hex(d.finish)
    return true if got == want
    @note = "the files do not match the list (#{got[0, 8]} vs #{want[0, 8]})"
    @job = nil
    false
  end

  def finish_install(job)
    a = job[:app]
    id = a["id"].to_s
    dir = app_dir(id)
    remove_files(id)
    Dir.mkdir(INSTALL_ROOT) unless Dir.exist?(INSTALL_ROOT)
    Dir.mkdir(dir) unless Dir.exist?(dir)
    large = large_memory_needed?(a)
    i = 0
    while i < job[:got].size
      name = job[:got][i][0]
      data = job[:got][i][1]
      data = with_large_memory(data) if large && name.end_with?(".app.toml")
      File.open("#{dir}/#{name}", "w") { |f| f.write(data) }
      i += 1
    end
    drop_launcher_cache
    @job = nil
    tail = job[:unchecked] ? " (size checked only)" : ""
    @note = if large
              "installed#{tail}, large pool. Rescan to see it."
            else
              "installed#{tail}. Right-click the launcher to rescan."
            end
  end

  # The firmware's key is a boolean and means a different number on every
  # machine, so the size travels in the list and the boolean is written here,
  # for here.
  def large_memory_needed?(a)
    kb = heap_kb(a)
    !kb.nil? && kb > @info[:pool]
  end

  def with_large_memory(text)
    return text if value_of(text, "large_memory")
    "#{text}\n# Written when this app was installed: it wants more heap than\n" \
    "# this machine's ordinary pool holds.\nlarge_memory = 1\n"
  end

  def remove_files(id)
    dir = app_dir(id)
    return unless Dir.exist?(dir)
    names = Dir.children(dir)
    i = 0
    while i < names.size
      n = names[i]
      i += 1
      next if n == "." || n == ".."
      path = "#{dir}/#{n}"
      File.delete(path) if File.exist?(path)
    end
    Dir.rmdir(dir) rescue nil
  end

  def remove_app(a)
    id = a["id"].to_s
    unless installed_version(id)
      @note = "it is not installed"
      return
    end
    remove_files(id)
    drop_launcher_cache
    @note = "removed. Rescan to update the launcher."
  end

  # Boot trusts this file without revalidating it, so an app that was just
  # added is invisible until it is gone (launcher.rb). The browser never has
  # one -- it is not among the directories that survive a reload -- so this
  # quietly does nothing there.
  def drop_launcher_cache
    File.delete(CACHE_PATH) if File.exist?(CACHE_PATH)
  rescue => e
    Log.warn("appstore: could not drop the launcher cache: #{e.message}")
  end

  # Give the display side its image slots back before this app goes.
  #
  # It has eight for the whole machine and frees one only when it is told to:
  # deleting a canvas does not release the images drawn on it
  # (display_p4_task.cpp calls image_store_destroy from DELETE_IMAGE and
  # nowhere else). An app that exits holding some leaks them until the board
  # is rebooted -- after a few launches every create_image fails, draw_image
  # is called with an id that is not there ("DRAW_IMAGE: image 0 not found"),
  # and the desktop's own sprite rebuild does not survive what is left.
  # That is what stopped the desktop after a launcher rescan on 2026-09-02.
  #
  # This has to happen in stop rather than on_destroy: destroy tears the gfx
  # down and nils it before on_destroy runs, so by then there is nothing to
  # ask (measured -- "undefined method 'delete_image' for NilClass").
  def stop
    release_images
    super
  end

  def release_images
    return unless @images
    @image_order.each do |id|
      img = @images[id]
      next unless img
      begin
        @gfx.delete_image(img) if @gfx
      rescue => e
        Log.warn("appstore: could not release image #{id}: #{e.message}")
      end
    end
    @images = {}
    @image_order = []
    nil
  end

  # ---- events -------------------------------------------------------------

  def on_event(ev)
    super(ev)
    id = @ui.handle(ev)
    if id
      case id
      when :f_find     then @face = :find;      @sel = 0; @scroll = 0
      when :f_inst     then @face = :installed; @sel = 0; @scroll = 0
      when :b_go       then a = current; start_install(a) if a
      when :b_del      then a = current; remove_app(a) if a
      when :b_refresh
        @note = "fetching the list..."
        @apps = []
        @req = FmrbNet.request(base + "registry.tsv")
      end
      draw_screen
      return
    end
    return unless ev[:type] == :mouse_up && ev[:button] == 1
    y = ev[:y]
    return if y < list_y || y >= list_y + list_h
    idx = @scroll + ((y - list_y) / row_h)
    items = shown
    return if idx >= items.size
    return if idx == @sel
    select(idx, items)
  end

  # Moving the selection repaints two rows and the detail area, not the
  # window. A full redraw is 300-440 ms on a board (most of it decoding the
  # pictures again), and that wait is what looked like a flicker.
  def select(idx, items)
    was = @sel
    @sel = idx
    draw_row(items[was], @user_area_x0, row_y(was), false) if row_visible?(was) && items[was]
    draw_row(items[idx], @user_area_x0, row_y(idx), true)
    draw_detail
    draw_thumb(items[idx])
    want_thumb(items[idx])
    @gfx.present
  end

  def on_update
    if @req && @req.done?
      load_registry(@req)
      @req = nil
      draw_screen
    end
    if @job
      before = @job[:idx]
      step_install
      draw_screen if @job.nil? || @job[:idx] != before
    end
    if step_thumb
      a = current
      draw_thumb(a)
      @gfx.present
    end
    80
  end

  # registry.tsv, not registry.json.
  #
  # picoruby's JSON parser took 7.6 SECONDS over the 2.8 KB list on an
  # ESP32-P4 (measured 2026-09-02) -- more than the fetch, the drawing and
  # every picture put together. The same list as tab-separated lines is a
  # split and nothing else. tools/registry.rb in family-mruby-apps writes
  # both from the same data, and this is the one machines read.
  #
  #   1                              the format version, on its own line
  #   A <TAB> id ... thumb_size      one app, fields in a fixed order
  #   F <TAB> path <TAB> size        a file of the app above it
  #
  # A line whose first field is not known is skipped, so a later version can
  # add records without breaking this.
  TSV_VERSION = "1"
  A_FIELDS = %w[id version name name_ja description description_ja
                category author env min_width min_height
                heap_esp32 heap_linux sha256 base thumb_path thumb_size]

  def load_registry(req)
    unless req.ok?
      @note = "list: #{req.error || "status #{req.status}"}"
      return
    end
    @note = "reading the list..."
    apps = parse_tsv(req.body.to_s)
    if apps.nil?
      @note = "the list is not one this store understands"
      return
    end
    if apps.empty?
      @note = "the list has no apps"
      return
    end
    @apps = apps
    @sel = 0
    @scroll = 0
    @note = "#{apps.size} apps - #{@env}"
    Log.info("appstore: #{apps.size} apps for #{@env}")
  rescue => e
    @note = "could not read the list: #{e.message}"
  end

  # nil when the version line is missing or is one we do not know.
  def parse_tsv(text)
    lines = text.split("\n")
    return nil if lines.empty?
    return nil unless lines[0].strip == TSV_VERSION
    apps = []
    current = nil
    i = 1
    while i < lines.size
      line = lines[i]
      i += 1
      next if line.nil? || line.empty?
      f = line.split("\t")
      kind = f[0]
      if kind == "A"
        current = {}
        j = 0
        while j < A_FIELDS.size
          current[A_FIELDS[j]] = f[j + 1].to_s
          j += 1
        end
        current["env"] = current["env"].split(",")
        current["files"] = []
        apps << current
      elsif kind == "F" && current
        current["files"] << { "path" => f[1].to_s, "size" => f[2].to_i }
      end
    end
    apps
  end
end

begin
  app = AppStoreApp.new
  app.start
rescue => e
  puts "appstore: #{e.message}"
end
