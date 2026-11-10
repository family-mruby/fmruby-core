# Receiving: whether this machine takes files and apps from other machines
# over Asterism, and from whom (doc/asterism_file_transfer T7).
#
# The screen of receive.toml, read and changed by FmrbReceiveConf
# (/usr/share/transfer/receive_conf.rb), the same way the shell's areceive
# changes it:
#
#   - the switch (on starts the transfer app; off lets it end once what is
#     coming in is in)
#   - per kind (files to /home/inbox, apps moved here) the senders allowed:
#     the ones written on this machine, each with a delete box, and the
#     console's (the [managed] part it sent, "from the console", shown and
#     not changed here). "*" is shown as Anyone; on the apps' list it is
#     marked "!" (anyone could run apps here)
#   - add: a name typed (Enter or Add), or one picked from the machines on
#     the network now (Nearby: this app connects to Asterism for it, as
#     itself, only once that list is asked for). Anyone is the first entry
#     there, asked about before it is added.
#
# Changes are written to /home/asterism/receive.toml (made from the
# system's copy the first time) only when its text changes.
#
# Drawn in the 8 px mixed font the widgets use (ASCII 6x8, kana 8x8), so a
# 320x240 screen holds the whole list.

require "/usr/share/transfer/receive_conf"

class ReceivingApp < FmrbApp
  ROW_H = 11
  TOP_H = 36          # the switch, the kinds
  NOTES_H = 19        # two lines of notes (8 px text, 11 px apart)
  MARGIN = 3          # kept free under the notes, above the window frame
  BOTTOM_H = 3 + 17 + NOTES_H + MARGIN  # the name and the buttons, the notes
  SB_W = 10
  DEL_W = 12
  RELOAD_MS = 3000    # receive.toml looked at again (the console may write it)
  NODES_MS = 1000     # the nodes on the network, while they are shown
  WARN = 0xE0         # red: anyone may run apps here
  APP = "receiving"

  def on_create
    FmrbI18n.add(
      "en" => { rc_on: "Receiving: on", rc_off: "Receiving: off", rc_files: "Files",
                rc_apps: "Apps (checkpoint)", rc_add: "Add", rc_near: "Nearby", rc_back: "Back",
                rc_yes: "Yes", rc_no: "No", rc_anyone: "Anyone", rc_console: "from the console",
                rc_none: "(nobody)", rc_conf_home: "Settings: yours (/home)",
                rc_conf_etc: "Settings: as shipped (/etc)", rc_no_file: "Settings: none (no file)",
                rc_off_note: "Off: nothing is taken, whatever the lists say.",
                rc_ask_any: "Take files from anyone on the same network?",
                rc_ask_any_ckpt: "Anyone could then run apps on this machine. Allow anyone?",
                rc_warn_ckpt: "! Anyone on the network can run apps here.",
                rc_connecting: "Connecting to Asterism...",
                rc_no_nodes: "(no other machine seen)", rc_pick: "Click one to add it.",
                rc_saved: "Saved.", rc_same: "No change.", rc_started: "Saved; the transfer app started.",
                rc_stopping: "Saved; the transfer app ends when done.", rc_bad_name: "Not a node name: ",
                rc_hand_bad: "receive.toml is not TOML; not changed." },
      "ja" => { rc_on: "受け取り: 有効", rc_off: "受け取り: 無効", rc_files: "ファイル",
                rc_apps: "アプリ (checkpoint)", rc_add: "追加", rc_near: "見える端末", rc_back: "戻る",
                rc_yes: "はい", rc_no: "いいえ", rc_anyone: "誰でも", rc_console: "console から",
                rc_none: "(なし)", rc_conf_home: "設定: 自分の (/home)",
                rc_conf_etc: "設定: 出荷時 (/etc)", rc_no_file: "設定: 無し (ファイルが無い)",
                rc_off_note: "無効: 一覧にかかわらず何も受け取らない。",
                rc_ask_any: "同じネットワークのだれからでも受け取ります。よいですか?",
                rc_ask_any_ckpt: "だれでもこの端末でアプリを動かせるようになります。よいですか?",
                rc_warn_ckpt: "! だれでもこの端末でアプリを動かせる設定です。",
                rc_connecting: "Asterism に接続中...",
                rc_no_nodes: "(ほかの端末が見えない)", rc_pick: "クリックで追加する。",
                rc_saved: "保存した。", rc_same: "変更なし。", rc_started: "保存した。転送のアプリを起動した。",
                rc_stopping: "保存した。転送のアプリは受け取り中の分の後に止まる。", rc_bad_name: "ノード名でない: ",
                rc_hand_bad: "receive.toml が TOML でないので変えない。" }
    )
    @kind = "file"
    @rows = []          # [[name, from_hand], ...] of the kind shown
    @scroll = 0
    @picking = false    # the nodes on the network are shown instead
    @nodes = []
    @asking = nil       # the name a confirmation is about
    @note = ""
    @link = nil
    @hits = FmrbUI::HitAreas.new
    @loaded_at = 0
    @nodes_at = 0
    layout
    build_ui
    load_conf
    draw_screen
  end

  # ---- layout (user-area relative) ----

  def layout
    @w = @user_area_width
    @h = @user_area_height
    @list_y = TOP_H
    @list_h = @h - TOP_H - BOTTOM_H
    @list_h = ROW_H if @list_h < ROW_H
    @rows_vis = @list_h / ROW_H
    @list_w = @w - SB_W - 2
    @ctl_y = @list_y + @list_h + 3
    @note_y = @ctl_y + 17
  end

  def build_ui
    @ui = FmrbUI.new(self)
    @ui.toggle(:on, 2, 2, 126, 14, tr(:rc_off), on_text: tr(:rc_on))
    @ui.toggle(:k_file, 2, 19, 70, 14, tr(:rc_files), group: :kind, on: true)
    @ui.toggle(:k_ckpt, 76, 19, 120, 14, tr(:rc_apps), group: :kind)
    @ui.scrollbar(:sb, @w - SB_W - 1, @list_y, SB_W, @list_h, 0, @rows_vis, 0)
    @ui.text_field(:name, 2, @ctl_y, 116, 14, "", max: ::FmrbReceiveConf::ENTRY_MAX)
    @ui.button(:add, 122, @ctl_y, 44, 14, tr(:rc_add))
    @ui.button(:near, 170, @ctl_y, 74, 14, tr(:rc_near))
    by = @list_y + @list_h - 18
    @ui.button(:yes, @list_w / 2 - 70, by, 64, 14, tr(:rc_yes))
    @ui.button(:no, @list_w / 2 + 6, by, 64, 14, tr(:rc_no))
    @ui.set_visible(:yes, false)
    @ui.set_visible(:no, false)
  end

  def tr(key)
    FmrbI18n.t(key)
  end

  # ---- receive.toml ----

  def load_conf
    @view = ::FmrbReceiveConf.view
    @loaded_at = Machine.board_millis
    @ui.set_on(:on, @view["enabled"] == true)
    rows_of_kind
  end

  def rows_of_kind
    k = @view[@kind]
    out = []
    add_rows(out, k["hand"], true)
    add_rows(out, k["managed"], false)
    @rows = out
    fix_scroll(@rows.size)
  end

  def add_rows(out, list, hand)
    return unless list.is_a?(Array)
    i = 0
    while i < list.size
      out << [list[i].to_s, hand]
      i += 1
    end
  end

  # A change by FmrbReceiveConf (a method name and its arguments), then the
  # transfer app told when the switch moved, and the note.
  def change(what, node = nil)
    unless @view["hand_ok"]
      @note = tr(:rc_hand_bad)
      return
    end
    begin
      written = case what
                when :on then ::FmrbReceiveConf.set_enabled(true)
                when :off then ::FmrbReceiveConf.set_enabled(false)
                when :allow then ::FmrbReceiveConf.allow(@kind, node)
                else ::FmrbReceiveConf.deny(@kind, node)
                end
      @note = written ? tr(:rc_saved) : tr(:rc_same)
      if what == :on || what == :off
        said = ::FmrbReceiveConf.tell_transfer(self, what == :on)
        @note = tr(:rc_started) if said == :started
        @note = tr(:rc_stopping) if said == :stopping && written
      end
      Log.info("receiving: #{what} #{node} (#{written ? 'written' : 'no change'})")
    rescue ArgumentError => e
      @note = e.message
      Log.warn("receiving: #{e.message}")
    end
    load_conf
  end

  # Add a name: Anyone asks first.
  def add_name(n)
    name = n.to_s.strip
    return if name.empty?
    unless ::FmrbReceiveConf.node_ok?(name)
      @note = "#{tr(:rc_bad_name)}#{name}"
      return
    end
    if name == ::FmrbReceiveConf::ANYONE
      ask(name)
      return
    end
    change(:allow, name)
    @ui.set_field_text(:name, "")
  end

  def ask(name)
    @asking = name
    @ui.set_visible(:yes, true)
    @ui.set_visible(:no, true)
  end

  def answered(yes)
    name = @asking
    @asking = nil
    @ui.set_visible(:yes, false)
    @ui.set_visible(:no, false)
    return unless yes
    change(:allow, name)
    @ui.set_field_text(:name, "")
    @picking = false
    @ui.set_text(:near, tr(:rc_near))
  end

  # ---- the machines on the network (Nearby) ----

  def start_link
    return if @link
    @link = ::FmrbAsterism::Link.new(self, APP, tag: "receiving")
  end

  def link_up(link)
    @nodes_at = 0
  end

  def link_down(link)
    nil
  end

  def update_nodes
    have = @view[@kind]["hand"] || []
    list = []
    list << ::FmrbReceiveConf::ANYONE unless have.include?(::FmrbReceiveConf::ANYONE)
    if @link && @link.connected?
      own = @link.node.to_s
      all = ::Asterism.nodes
      i = 0
      while i < all.size
        n = all[i].to_s
        list << n unless n == own || have.include?(n) || list.include?(n)
        i += 1
      end
    end
    changed = list != @nodes
    @nodes = list
    fix_scroll(@nodes.size) if changed
    changed
  end

  def fix_scroll(total)
    max = total - @rows_vis
    max = 0 if max < 0
    @scroll = max if @scroll > max
    @scroll = 0 if @scroll < 0
    @ui.set_range(:sb, total, @rows_vis)
    @ui.set_value(:sb, @scroll)
  end

  # ---- drawing ----

  def draw_screen
    clear_user_area
    draw_body
  end

  def draw_body
    draw_path
    draw_list
    draw_notes
    @ui.flush
    @gfx.present
  end

  def ax(x)
    @user_area_x0 + x
  end

  def ay(y)
    @user_area_y0 + y
  end

  # Which receive.toml is in force: the user's own copy in /home, or the
  # one shipped in /etc (short, so it fits beside the switch).
  def draw_path
    x = ax(132)
    y = ay(5)
    w = @w - 134
    @gfx.fill_rect(x, y, w, 8, theme_bg)
    p = @view["path"]
    key = if p == ::FmrbReceiveConf::PATH then :rc_conf_home
          elsif p then :rc_conf_etc
          else :rc_no_file
          end
    @gfx.draw_text_mixed(x, y, @gfx.fit_text(tr(key), w, "..", nil, :default), theme_border)
  end

  def draw_list
    x = ax(0)
    y = ay(@list_y)
    @gfx.fill_rect(x, y, @list_w + 1, @list_h, theme_bg)
    @gfx.draw_rect(x + 1, y, @list_w, @list_h, theme_border)
    @hits.clear
    return draw_question(x, y) if @asking
    items = @picking ? @nodes : @rows
    if items.empty?
      @gfx.draw_text_mixed(x + 4, y + 2, tr(@picking ? :rc_no_nodes : :rc_none), theme_border)
      return
    end
    i = 0
    while i < @rows_vis
      idx = @scroll + i
      break if idx >= items.size
      ry = y + 1 + i * ROW_H
      if @picking
        draw_node_row(x, ry, items[idx].to_s)
      else
        draw_row(x, ry, items[idx])
      end
      i += 1
    end
    if @picking && (@link.nil? || !@link.connected?)
      @gfx.draw_text_mixed(x + 4, y + 1 + @rows_vis * ROW_H - ROW_H, tr(:rc_connecting), theme_border)
    end
  end

  def shown_name(n)
    n == ::FmrbReceiveConf::ANYONE ? "#{tr(:rc_anyone)} (*)" : n
  end

  # An allowed sender: its name, where it comes from, and a delete box for
  # the ones written here. (The click targets are canvas coordinates, as
  # the events are.)
  def draw_row(x, ry, row)
    name = row[0]
    hand = row[1]
    warn = @kind == "checkpoint" && name == ::FmrbReceiveConf::ANYONE
    color = warn ? WARN : theme_fg
    tag = hand ? "" : "  [#{tr(:rc_console)}]"
    room = @list_w - DEL_W - 10
    s = "#{warn ? '! ' : ''}#{shown_name(name)}#{tag}"
    @gfx.draw_text_mixed(x + 4, ry + 1, @gfx.fit_text(s, room, "..", nil, :default), hand ? color : theme_border)
    return unless hand
    bx = x + @list_w - DEL_W - 2
    @gfx.draw_rect(bx, ry, DEL_W, ROW_H - 1, theme_border)
    @gfx.draw_text_mixed(bx + 3, ry + 1, "x", theme_fg)
    @hits.add(:del, bx, ry, DEL_W, ROW_H - 1, name)
  end

  def draw_node_row(x, ry, n)
    @gfx.draw_text_mixed(x + 4, ry + 1, @gfx.fit_text(shown_name(n), @list_w - 8, "..", nil, :default), theme_fg)
    @hits.add(:pick, x, ry, @list_w, ROW_H, n)
  end

  def draw_question(x, y)
    key = @kind == "checkpoint" ? :rc_ask_any_ckpt : :rc_ask_any
    color = @kind == "checkpoint" ? WARN : theme_fg
    text = tr(key)
    w = @list_w - 12
    first = @gfx.fit_text(text, w, "", nil, :default)
    rest = text.byteslice(first.bytesize, text.bytesize - first.bytesize).to_s
    @gfx.draw_text_mixed(x + 6, y + 6, first, color)
    @gfx.draw_text_mixed(x + 6, y + 17, @gfx.fit_text(rest, w, "..", nil, :default), color) unless rest.empty?
  end

  # Two lines: the state (off, or anyone may run apps) and the last note.
  def draw_notes
    x = ax(0)
    y = ay(@note_y)
    @gfx.fill_rect(x, y, @w, NOTES_H, theme_bg)
    first = ""
    color = theme_border
    if @view["enabled"] != true
      first = tr(:rc_off_note)
    elsif anyone_runs_apps?
      first = tr(:rc_warn_ckpt)
      color = WARN
    elsif @picking
      first = tr(:rc_pick)
    end
    first = tr(:rc_warn_ckpt) if first.empty? && anyone_runs_apps?
    @gfx.draw_text_mixed(x + 2, y, @gfx.fit_text(first, @w - 4, "..", nil, :default), color)
    @gfx.draw_text_mixed(x + 2, y + 11, @gfx.fit_text(@note.to_s, @w - 4 - resize_corner, "..", nil, :default), theme_fg)
  end

  def anyone_runs_apps?
    c = @view["checkpoint"]
    ::FmrbReceiveConf.allows_anyone?(c["hand"]) || ::FmrbReceiveConf.allows_anyone?(c["managed"])
  end

  # ---- events ----

  def on_event(ev)
    super(ev)
    rows = wheel_rows(ev)
    if rows
      scroll_by(rows)
      return
    end
    id = @ui.handle(ev)
    if @asking
      answered(id == :yes) if id == :yes || id == :no
      draw_body if id
      @ui.flush if id.nil?
      return
    end
    case id
    when :on then change(@ui.on?(:on) ? :on : :off)
    when :k_file, :k_ckpt
      @kind = id == :k_file ? "file" : "checkpoint"
      @scroll = 0
      rows_of_kind
      update_nodes if @picking
    when :name, :add then add_name(@ui.field_text(:name))
    when :near then toggle_picking
    when :sb then @scroll = @ui.value(:sb)
    when nil
      return @ui.flush unless clicked(ev)
    end
    draw_body
  end

  # A click on the list (delete, or a node picked). True when it was one.
  def clicked(ev)
    return false unless ev[:type] == :mouse_down && ev[:button].to_i == 1
    b = @hits.at(ev[:x].to_i, ev[:y].to_i)
    return false if b.nil?
    name = b[5].to_s
    if b[0] == :del
      change(:deny, name)
    elsif name == ::FmrbReceiveConf::ANYONE
      ask(name)
    else
      change(:allow, name)
      update_nodes
    end
    true
  end

  def toggle_picking
    @picking = !@picking
    @scroll = 0
    if @picking
      start_link
      update_nodes
      @nodes_at = Machine.board_millis
      @ui.set_text(:near, tr(:rc_back))
    else
      fix_scroll(@rows.size)
      @ui.set_text(:near, tr(:rc_near))
    end
  end

  def scroll_by(rows)
    total = @picking ? @nodes.size : @rows.size
    @scroll += rows
    fix_scroll(total)
    draw_body
  end

  def on_update
    now = Machine.board_millis
    redraw = false
    if @link
      before = @link.connected?
      @link.update
      redraw = true if before != @link.connected? && @picking
    end
    if @picking && now - @nodes_at >= NODES_MS
      @nodes_at = now
      redraw = true if update_nodes
    end
    if @asking.nil? && now - @loaded_at >= RELOAD_MS
      old = @view
      load_conf
      redraw = true if old != @view
    end
    draw_body if redraw
    @link ? 50 : 300
  end

  def on_destroy
    @link.close if @link
  end
end

begin
  app = ReceivingApp.new
  app.start
rescue => e
  Log.error("receiving: #{e}")
end
