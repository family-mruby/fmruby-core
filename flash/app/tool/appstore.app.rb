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
  BASE = "https://raw.githubusercontent.com/family-mruby/family-mruby-apps/main/"
  REGISTRY = BASE + "registry.json"
  INSTALL_ROOT = "/app/usr"
  CACHE_PATH = "/var/cache/launcher_index"

  ROW_H = 11
  TAB_H = 16

  # What each machine gives an app, from fmrb_mem_config.h and the display
  # settings. The store has to know these to answer "does this fit here".
  ENV_INFO = {
    "retro"  => { w: 320, h: 240, pool: 500,  large: 1024 },
    "modern" => { w: 426, h: 240, pool: 1024, large: 2048 },
    "web"    => { w: 426, h: 240, pool: 1536, large: 3072 },
  }

  def on_create
    @env = detect_env
    @info = ENV_INFO[@env] || ENV_INFO["modern"]
    @face = :find              # :find or :installed
    @apps = []
    @sel = 0
    @scroll = 0
    @note = "loading the list..."
    @job = nil                 # an install in progress
    @req = FmrbNet.request(REGISTRY)
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
  def bar_y
    @user_area_y0 + TAB_H
  end

  # 16 for the buttons and 8 of clearance: FmrbUI's flush paints a little
  # past the widgets, and a tighter gap eats the first row.
  def list_y
    bar_y + 24
  end

  def detail_y
    @user_area_y1 - 41
  end

  def list_h
    h = detail_y - list_y - 2
    h < ROW_H ? ROW_H : h
  end

  def rows_visible
    n = list_h / ROW_H
    n < 1 ? 1 : n
  end

  def build_ui
    w = area_w
    x = @user_area_x0
    y = @user_area_y0
    @ui.toggle(:f_find, x, y, 62, TAB_H - 2, "Find", group: :face, on: true)
    @ui.toggle(:f_inst, x + 64, y, 78, TAB_H - 2, "Installed", group: :face)
    by = bar_y
    @ui.button(:b_go, x, by, 60, 16, "Install")
    @ui.button(:b_del, x + 64, by, 58, 16, "Remove")
    @ui.button(:b_refresh, x + w - 60, by, 60, 16, "Reload")
  end

  # ---- drawing ------------------------------------------------------------

  def draw_screen
    clear_user_area
    # FmrbUI paints its own area on flush, which reaches a little past the
    # widgets themselves -- draw the list after it, or the first row goes
    # missing under the button bar.
    @ui.flush
    draw_list
    draw_detail
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
      ry = y + i * ROW_H
      # A marker rather than a highlight bar: on a 320 px screen the row is
      # only 11 px tall, and a filled bar leaves nowhere for the ink to be
      # legible against both themes.
      sel = (idx == @sel)
      fg = fits?(a) ? theme_fg : theme_border
      @gfx.draw_text(x + 2, ry + 1, (sel ? ">" : " ") + row_label(a), fg, theme_bg)
      i += 1
    end
  end

  def row_label(a)
    id = a["id"].to_s
    have = installed_version(id)
    mark = if have.nil?
             " "
           elsif have == a["version"]
             "="
           else
             "^"
           end
    name = a["name"].to_s
    # The label is drawn at 6 px a character; leave room for the marker and
    # the version at the far end.
    room = (area_w / 6) - 10
    name = name[0, room] if room > 0 && name.length > room
    "#{mark} #{name}"
  end

  def draw_detail
    x = @user_area_x0
    y = detail_y
    w = area_w
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

  # nil when it can run here, otherwise the reason in one short line.
  def blocked_reason(a)
    envs = a["env"]
    if envs.is_a?(Array) && !envs.include?(@env)
      return "not checked on #{@env}"
    end
    mw = a["min_width"]
    return "needs #{mw}px wide" if mw.is_a?(Integer) && mw > @info[:w]
    mh = a["min_height"]
    return "needs #{mh}px tall" if mh.is_a?(Integer) && mh > @info[:h]
    kb = heap_kb(a)
    return "needs #{kb}KB heap" if kb && kb > @info[:large]
    nil
  end

  # The number for this machine's object model: boards are 32-bit, the
  # simulator and the browser share the linux pool sizes.
  def heap_kb(a)
    h = a["required_heap_kb"]
    return nil unless h.is_a?(Hash)
    key = (@env == "web") ? "linux" : "esp32"
    v = h[key]
    v.is_a?(Integer) ? v : nil
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
    @job = { app: a, files: files, idx: 0, got: [], req: nil }
    @note = "getting 1/#{files.size}..."
  end

  # Driven from on_update. Everything is fetched before anything is written,
  # so a failure halfway through cannot leave half an app behind.
  def step_install
    job = @job
    return unless job
    if job[:req].nil?
      f = job[:files][job[:idx]]
      job[:req] = FmrbNet.request(BASE + job[:app]["base"].to_s + f["path"].to_s)
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
    # Size is the only check available here: there is no digest on this
    # machine, so a sha256 in the list cannot be verified yet.
    if size.is_a?(Integer) && body.bytesize != size
      @note = "#{f['path']}: #{body.bytesize}B, expected #{size}B"
      @job = nil
      return
    end
    job[:got] << [f["path"].to_s, body]
    job[:req] = nil
    job[:idx] += 1
    if job[:idx] >= job[:files].size
      finish_install(job)
    else
      @note = "getting #{job[:idx] + 1}/#{job[:files].size}..."
    end
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
    @note = large ? "installed (large pool). Rescan to see it." :
                    "installed. Right-click the launcher to rescan."
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
        @note = "loading the list..."
        @apps = []
        @req = FmrbNet.request(REGISTRY)
      end
      draw_screen
      return
    end
    return unless ev[:type] == :mouse_up && ev[:button] == 1
    y = ev[:y]
    return if y < list_y || y >= list_y + list_h
    idx = @scroll + ((y - list_y) / ROW_H)
    items = shown
    return if idx >= items.size
    @sel = idx
    draw_screen
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
    80
  end

  def load_registry(req)
    unless req.ok?
      @note = "list: #{req.error || "status #{req.status}"}"
      return
    end
    data = ::JSON.parse(req.body.to_s)
    apps = data.is_a?(Hash) ? data["apps"] : nil
    unless apps.is_a?(Array)
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
end

begin
  app = AppStoreApp.new
  app.start
rescue => e
  puts "appstore: #{e.message}"
end
