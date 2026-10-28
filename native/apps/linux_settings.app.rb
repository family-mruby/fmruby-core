# The PC's settings window (doc/linux_native, L14): Wi-Fi and Bluetooth.
#
# Built into the native program only (native/backend/native_settings.c
# spawns it from its bytecode; no device, simulation or browser build has
# it): the desktop's network dialog opens it on the Wi-Fi page ("Wi-Fi..."),
# its menu on the Bluetooth page. Asking again brings it in front and turns
# the page (FmrbApp.settings_page counts the asks).
#
# Wi-Fi (NetworkManager, native/sys/linux_net.h): the networks in reach, the
# strongest of each name, the one in use first; choose one and Connect. A
# secured network that NetworkManager has not saved asks for its password
# here (typed into the field, shown as stars); it goes to NetworkManager,
# which keeps it as its own tools do, and is cleared from this window when
# the attempt is made. Disconnect leaves the network saved. Scan asks for
# a fresh look.
#
# Bluetooth (BlueZ, native/sys/linux_bt.h): the adapter on or off, the
# devices paired before, Connect and Disconnect. A new device is paired in
# Linux's own tool (one line says which).
#
# Everything here only asks: the C side does the work on threads of its
# own and this window reads the outcome about once a second.

class LinuxSettingsApp < FmrbApp
  ROW_H = 12
  ROWS = 7
  LIST_Y = 32
  POLL_MS = 1000

  STRINGS = {
    "en" => {
      st_wifi: "Wi-Fi", st_bt: "Bluetooth",
      st_scan: "Scan", st_connect: "Connect", st_disconnect: "Disconnect",
      st_bt_on: "Turn on", st_bt_off: "Turn off",
      st_password: "Password:",
      st_wifi_using: "Connected to %s", st_wifi_none_used: "Not connected",
      st_wifi_no_nm: "NetworkManager is not running",
      st_wifi_no_dev: "No Wi-Fi device", st_wifi_off: "Wi-Fi is off (turn it on in Linux)",
      st_wifi_starting: "Asking NetworkManager...", st_wifi_empty: "No networks in reach",
      st_scanning: "Scanning...", st_connecting: "Connecting to %s...",
      st_connected: "Connected to %s", st_disconnected: "Disconnected",
      st_need_password: "%s needs its password", st_auth_failed: "%s: the password was not accepted",
      st_no_network: "%s is not in reach", st_enterprise: "%s needs settings made in Linux",
      st_failed: "Could not connect to %s", st_refused: "Refused: %s",
      st_type_password: "Type the password, then Connect",
      st_bt_none: "No Bluetooth adapter (BlueZ)", st_bt_starting: "Asking BlueZ...",
      st_bt_is_on: "Bluetooth is on", st_bt_is_off: "Bluetooth is off",
      st_bt_empty: "No paired devices", st_bt_pair: "New devices: pair them in Linux (bluetoothctl)",
      st_bt_busy: "Working on %s...", st_bt_connected: "Connected: %s", st_bt_disconnected: "Disconnected: %s",
      st_bt_failed: "Failed: %s", st_bt_power_failed: "Could not switch: %s",
      st_bt_powered_on: "Bluetooth turned on", st_bt_powered_off: "Bluetooth turned off",
      st_saved: "saved"
    },
    "ja" => {
      st_wifi: "Wi-Fi", st_bt: "Bluetooth",
      st_scan: "探す", st_connect: "つなぐ", st_disconnect: "切る",
      st_bt_on: "入れる", st_bt_off: "切る",
      st_password: "パスワード:",
      st_wifi_using: "%s につながっています", st_wifi_none_used: "つながっていません",
      st_wifi_no_nm: "NetworkManager が動いていません",
      st_wifi_no_dev: "Wi-Fi の装置がありません", st_wifi_off: "Wi-Fi が切れています (Linux で入れる)",
      st_wifi_starting: "NetworkManager に聞いています...", st_wifi_empty: "届くネットワークがありません",
      st_scanning: "探しています...", st_connecting: "%s につないでいます...",
      st_connected: "%s につながりました", st_disconnected: "切りました",
      st_need_password: "%s のパスワードを入れてください", st_auth_failed: "%s: パスワードが違うようです",
      st_no_network: "%s が見つかりません", st_enterprise: "%s は Linux で設定してください",
      st_failed: "%s につなげませんでした", st_refused: "断られました: %s",
      st_type_password: "パスワードを入れて「つなぐ」",
      st_bt_none: "Bluetooth の装置がありません (BlueZ)", st_bt_starting: "BlueZ に聞いています...",
      st_bt_is_on: "Bluetooth は入っています", st_bt_is_off: "Bluetooth は切れています",
      st_bt_empty: "ペアリング済みの機器がありません", st_bt_pair: "新しい機器は Linux の bluetoothctl でペアリング",
      st_bt_busy: "%s を操作しています...", st_bt_connected: "つなぎました: %s", st_bt_disconnected: "切りました: %s",
      st_bt_failed: "できませんでした: %s", st_bt_power_failed: "切り替えられませんでした: %s",
      st_bt_powered_on: "Bluetooth を入れました", st_bt_powered_off: "Bluetooth を切りました",
      st_saved: "保存"
    }
  }

  def on_create
    FmrbI18n.add(STRINGS)
    @gfx.set_font(:default)
    @gfx.set_text_size(1)
    @w = @user_area_width
    @h = @user_area_height
    @ask = FmrbApp.settings_page
    @page = (@ask & 1) == 1 ? :bt : :wifi
    @wifi_text = nil
    @bt_text = nil
    @aps = []
    @devs = []
    @sel = 0
    @sel_name = nil
    @top = 0
    @pw = ""
    @msg = ""
    build_widgets
    Log.info("settings: the #{@page == :wifi ? 'Wi-Fi' : 'Bluetooth'} page, user area #{@w}x#{@h} at (#{@user_area_x0},#{@user_area_y0})")
    read_state(true)
    draw_page
  end

  def on_update
    ask = FmrbApp.settings_page
    if ask != @ask
      @ask = ask
      switch_page((ask & 1) == 1 ? :bt : :wifi)
    end
    draw_page if read_state(false)
    POLL_MS
  end

  def on_event(ev)
    t = ev[:type]
    id = @ui.handle(ev)
    if id
      act(id)
      return
    end
    if t == :mouse_down && ev[:button] == 1
      click_list(ev[:x].to_i - @user_area_x0, ev[:y].to_i - @user_area_y0)
    elsif t == :mouse_wheel
      scroll(ev[:delta].to_i > 0 ? -1 : 1)
    elsif t == :key_down
      key(ev[:scancode].to_i, ev[:character].to_i)
    end
    @ui.flush
  end

  def on_destroy
    @pw = ""
  end

  private

  # ---- widgets

  def build_widgets
    @ui = FmrbUI.new(self)
    tw = 56
    @ui.toggle(:tab_wifi, 2, 2, tw, 14, FmrbI18n.t(:st_wifi), group: :page, on: @page == :wifi)
    @ui.toggle(:tab_bt, 2 + tw + 4, 2, tw + 10, 14, FmrbI18n.t(:st_bt), group: :page, on: @page == :bt)
    by = @h - 34
    bw = 72
    @ui.button(:connect, 4, by, bw, 16, FmrbI18n.t(:st_connect))
    @ui.button(:disconnect, 4 + bw + 6, by, bw, 16, FmrbI18n.t(:st_disconnect))
    @ui.button(:scan, 4 + (bw + 6) * 2, by, bw, 16, FmrbI18n.t(:st_scan))
    @ui.button(:power, @w - 64, 18, 60, 13, FmrbI18n.t(:st_bt_off))
    show_widgets
  end

  def show_widgets
    wifi = @page == :wifi
    @ui.set_visible(:scan, wifi)
    @ui.set_visible(:power, !wifi)
  end

  def switch_page(page)
    return if page == @page
    @page = page
    @sel = 0
    @sel_name = nil
    @top = 0
    @msg = ""
    @ui.set_on(:tab_wifi, page == :wifi)
    @ui.set_on(:tab_bt, page == :bt)
    show_widgets
    read_state(true)
    draw_page
  end

  def act(id)
    case id
    when :tab_wifi then switch_page(:wifi)
    when :tab_bt then switch_page(:bt)
    when :scan
      @wifi_text = FmrbApp.wifi_networks(true)
      parse_wifi
      draw_page
    when :connect then do_connect
    when :disconnect then do_disconnect
    when :power
      FmrbApp.bt_power(!@bt_powered)
      draw_page
    end
  end

  # ---- state

  # Reads the page's state; true when it changed.
  def read_state(force)
    if @page == :wifi
      t = FmrbApp.wifi_networks(false)
      return false if !force && t == @wifi_text
      @wifi_text = t
      parse_wifi
      log_state("wifi", @wifi_head, @aps)
    else
      t = FmrbApp.bt_devices
      return false if !force && t == @bt_text
      @bt_text = t
      parse_bt
      log_state("bt", @bt_head, @devs)
    end
    true
  end

  # One line per change, for the log (and the checks): the state fields and
  # the names in the list ("*" active or connected).
  def log_state(what, head, rows)
    names = []
    i = 0
    while i < rows.size
      r = rows[i]
      if what == "wifi"
        names << "#{r[0]}#{r[3] ? '*' : ''}"
      else
        names << "#{r[1]}#{r[2] ? '*' : ''}"
      end
      i += 1
    end
    Log.info("settings: #{what} #{head[1]} [#{head[2]}] [#{head[3]}] [#{head[4]}] [#{head[5]}] #{names.join(',')}")
  end

  # The tab-separated fields; empty ones at the end are kept (as "").
  def fields(line)
    f = line.split("\t")
    f << "" while f.size < 6
    f
  end

  def parse_wifi
    @aps = []
    @wifi_head = ["wifi", "starting", "", "", "", ""]
    return if @wifi_text.nil? || @wifi_text.empty?
    lines = @wifi_text.split("\n")
    i = 0
    while i < lines.size
      f = fields(lines[i])
      if f[0] == "wifi"
        @wifi_head = f
      elsif f[0] == "ap" && f.size >= 6
        @aps << [f[1], f[2].to_i, f[3] == "1", f[4] == "1", f[5] == "1"]
      end
      i += 1
    end
    keep_selection(@aps, 0)
  end

  def parse_bt
    @devs = []
    @bt_head = ["bt", "starting", "0", "", "", ""]
    return if @bt_text.nil? || @bt_text.empty?
    lines = @bt_text.split("\n")
    i = 0
    while i < lines.size
      f = fields(lines[i])
      if f[0] == "bt"
        @bt_head = f
      elsif f[0] == "dev" && f.size >= 4
        @devs << [f[1], f[2], f[3] == "1", f[4].to_s]
      end
      i += 1
    end
    @bt_powered = @bt_head[2] == "1"
    @ui.set_text(:power, FmrbI18n.t(@bt_powered ? :st_bt_off : :st_bt_on))
    keep_selection(@devs, 0)
  end

  # The selection follows its network or device when the list reorders
  # (the one connected moves to the top).
  def keep_selection(rows, key)
    if @sel_name
      i = 0
      while i < rows.size
        if rows[i][key] == @sel_name
          @sel = i
          break
        end
        i += 1
      end
    end
    fix_selection(rows.size)
    r = rows[@sel]
    @sel_name = r ? r[key] : nil
  end

  def fix_selection(n)
    @sel = n - 1 if @sel >= n
    @sel = 0 if @sel < 0
    @top = @sel if @sel < @top
    @top = @sel - ROWS + 1 if @sel >= @top + ROWS
    @top = 0 if @top < 0
  end

  def head(i)
    h = @page == :wifi ? @wifi_head : @bt_head
    (h && h[i]) || ""
  end

  def fmt(key, arg)
    FmrbI18n.t(key).sub("%s", arg.to_s)
  end

  # ---- actions

  def selected_ap
    @aps[@sel]
  end

  # The password field shows for a secured network NetworkManager has not
  # saved, and after an attempt said the password was wanted or wrong.
  def needs_password?
    return false unless @page == :wifi
    ap = selected_ap
    return false if ap.nil?
    code = head(4)
    asked = (code == "need_password" || code == "auth_failed") && head(5) == ap[0]
    (ap[2] && !ap[4]) || asked
  end

  def do_connect
    if @page == :wifi
      ap = selected_ap
      return if ap.nil?
      if needs_password? && @pw.empty?
        @msg = FmrbI18n.t(:st_type_password)
        draw_page
        return
      end
      pw = @pw
      @pw = ""
      FmrbApp.wifi_connect(ap[0], pw)
      pw = nil
      @msg = ""
    else
      d = @devs[@sel]
      return if d.nil?
      FmrbApp.bt_connect(d[0], true)
    end
    read_state(true)
    draw_page
  end

  def do_disconnect
    if @page == :wifi
      FmrbApp.wifi_disconnect
    else
      d = @devs[@sel]
      return if d.nil?
      FmrbApp.bt_connect(d[0], false)
    end
    read_state(true)
    draw_page
  end

  def click_list(x, y)
    return if y < LIST_Y || y >= LIST_Y + ROWS * ROW_H
    i = @top + (y - LIST_Y) / ROW_H
    n = @page == :wifi ? @aps.size : @devs.size
    return if i >= n
    select(i)
  end

  def select(i)
    return if i == @sel
    @sel = i
    @pw = ""
    @msg = ""
    rows = @page == :wifi ? @aps : @devs
    fix_selection(rows.size)
    r = rows[@sel]
    @sel_name = r ? r[0] : nil
    draw_page
  end

  def scroll(d)
    n = @page == :wifi ? @aps.size : @devs.size
    t = @top + d
    t = n - ROWS if t > n - ROWS
    t = 0 if t < 0
    return if t == @top
    @top = t
    draw_page
  end

  def key(sc, ch)
    n = @page == :wifi ? @aps.size : @devs.size
    if sc == FmrbConst::KEY_UP
      select(@sel - 1) if @sel > 0
    elsif sc == FmrbConst::KEY_DOWN
      select(@sel + 1) if @sel + 1 < n
    elsif ch == 13 || ch == 10
      do_connect
    elsif needs_password?
      if ch == 8
        @pw = @pw[0, @pw.length - 1] if @pw.length > 0
        draw_page
      elsif ch >= 32 && ch <= 126 && @pw.length < 63
        @pw = @pw + ch.chr
        draw_page
      end
    end
  end

  # ---- drawing

  def ax(x)
    @user_area_x0 + x
  end

  def ay(y)
    @user_area_y0 + y
  end

  def draw_page
    clear_user_area
    if @page == :wifi
      draw_wifi
    else
      draw_bt
    end
    @ui.invalidate_all
    @ui.flush
  end

  def text(x, y, s, color = nil)
    @gfx.draw_text(ax(x), ay(y), s, color || theme_fg, theme_bg, mixed: true)
  end

  def fit(s, px)
    FmrbI18n.truncate_to(s.to_s, px)
  end

  def status_line(y, s)
    text(4, y, fit(s, @w - 8))
  end

  def draw_wifi
    st = head(1)
    line = case st
           when "none" then FmrbI18n.t(:st_wifi_no_nm)
           when "nodevice" then FmrbI18n.t(:st_wifi_no_dev)
           when "off" then FmrbI18n.t(:st_wifi_off)
           when "starting" then FmrbI18n.t(:st_wifi_starting)
           else
             head(2).empty? ? FmrbI18n.t(:st_wifi_none_used) : fmt(:st_wifi_using, head(2))
           end
    status_line(20, line)
    if st == "ok" && @aps.empty?
      text(8, LIST_Y + 2, FmrbI18n.t(:st_wifi_empty), theme_border)
    end
    i = 0
    while i < ROWS
      k = @top + i
      break if k >= @aps.size
      draw_ap_row(LIST_Y + i * ROW_H, @aps[k], k == @sel)
      i += 1
    end
    draw_scroll_mark(@aps.size)
    py = LIST_Y + ROWS * ROW_H + 3
    if needs_password?
      text(4, py + 2, FmrbI18n.t(:st_password))
      bx = 70
      bw = @w - bx - 6
      @gfx.fill_rect(ax(bx), ay(py), bw, 12, theme_fg_light)
      @gfx.draw_rect(ax(bx), ay(py), bw, 12, theme_accent)
      stars = "*" * @pw.length
      stars = stars[stars.length - 30, 30] if stars.length > 30
      @gfx.draw_text(ax(bx + 2), ay(py + 2), stars, theme_fg, theme_fg_light)
    end
    draw_message(wifi_outcome)
  end

  def wifi_outcome
    return @msg unless @msg.empty?
    busy = head(3)
    return fmt(:st_connecting, head(5)) if busy == "connecting"
    return FmrbI18n.t(:st_scanning) if busy == "scanning"
    code = head(4)
    arg = head(5)
    case code
    when "connected" then fmt(:st_connected, arg)
    when "disconnected" then FmrbI18n.t(:st_disconnected)
    when "need_password" then fmt(:st_need_password, arg)
    when "auth_failed" then fmt(:st_auth_failed, arg)
    when "no_network" then fmt(:st_no_network, arg)
    when "enterprise" then fmt(:st_enterprise, arg)
    when "failed" then fmt(:st_failed, arg)
    when "refused" then fmt(:st_refused, arg)
    else ""
    end
  end

  def draw_ap_row(y, ap, selected)
    bg = selected ? theme_accent : theme_bg
    fg = selected ? theme_fg_light : theme_fg
    @gfx.fill_rect(ax(2), ay(y), @w - 12, ROW_H, bg)
    # the strength: four bars
    bars = (ap[1] + 12) / 25
    bars = 4 if bars > 4
    j = 0
    while j < 4
      bh = 2 + j * 2
      c = j < bars ? fg : (selected ? theme_fg_light : theme_border)
      if j < bars
        @gfx.fill_rect(ax(5 + j * 3), ay(y + 10 - bh), 2, bh, c)
      else
        @gfx.fill_rect(ax(5 + j * 3), ay(y + 9), 2, 1, c)
      end
      j += 1
    end
    name = ap[0]
    name = "> " + name if ap[3]
    right = ""
    right = FmrbI18n.t(:st_saved) if ap[4]
    rw = FmrbI18n.text_width(right)
    @gfx.draw_text(ax(20), ay(y + 2), fit(name, @w - 50 - rw), fg, bg, mixed: true)
    if ap[2]
      # a lock: a small box with a loop
      lx = @w - 20
      @gfx.fill_rect(ax(lx), ay(y + 5), 6, 5, fg)
      @gfx.draw_rect(ax(lx + 1), ay(y + 2), 4, 4, fg)
    end
    @gfx.draw_text(ax(@w - 24 - rw), ay(y + 2), right, fg, bg, mixed: true) unless right.empty?
  end

  def draw_scroll_mark(n)
    return if n <= ROWS
    x = @w - 8
    top = LIST_Y
    h = ROWS * ROW_H
    @gfx.fill_rect(ax(x), ay(top), 3, h, theme_border)
    th = h * ROWS / n
    th = 6 if th < 6
    ty = top + (h - th) * @top / (n - ROWS)
    @gfx.fill_rect(ax(x), ay(ty), 3, th, theme_fg)
  end

  def draw_message(s)
    return if s.nil? || s.empty?
    text(4, @h - 14, fit(s, @w - 8), theme_accent)
  end

  def draw_bt
    st = head(1)
    line = case st
           when "none" then FmrbI18n.t(:st_bt_none)
           when "starting" then FmrbI18n.t(:st_bt_starting)
           else FmrbI18n.t(@bt_powered ? :st_bt_is_on : :st_bt_is_off)
           end
    status_line(20, line)
    @ui.set_visible(:power, st == "ok")
    if st == "ok" && @devs.empty?
      text(8, LIST_Y + 2, FmrbI18n.t(:st_bt_empty), theme_border)
    end
    i = 0
    while i < ROWS
      k = @top + i
      break if k >= @devs.size
      d = @devs[k]
      y = LIST_Y + i * ROW_H
      sel = k == @sel
      bg = sel ? theme_accent : theme_bg
      fg = sel ? theme_fg_light : theme_fg
      @gfx.fill_rect(ax(2), ay(y), @w - 12, ROW_H, bg)
      @gfx.fill_circle(ax(8), ay(y + 6), 3, fg) if d[2]
      @gfx.draw_circle(ax(8), ay(y + 6), 3, fg) unless d[2]
      @gfx.draw_text(ax(16), ay(y + 2), fit(d[1], @w - 110), fg, bg, mixed: true)
      @gfx.draw_text(ax(@w - 96), ay(y + 2), d[0], fg, bg, mixed: true)
      i += 1
    end
    draw_scroll_mark(@devs.size)
    text(4, LIST_Y + ROWS * ROW_H + 4, fit(FmrbI18n.t(:st_bt_pair), @w - 8), theme_border)
    draw_message(bt_outcome)
  end

  def bt_outcome
    busy = head(3)
    unless busy.empty?
      name = busy
      i = 0
      while i < @devs.size
        name = @devs[i][1] if @devs[i][0] == busy
        i += 1
      end
      return fmt(:st_bt_busy, name)
    end
    arg = head(5)
    case head(4)
    when "connected" then fmt(:st_bt_connected, arg)
    when "disconnected" then fmt(:st_bt_disconnected, arg)
    when "failed" then fmt(:st_bt_failed, arg)
    when "power_failed" then fmt(:st_bt_power_failed, arg)
    when "powered_on" then FmrbI18n.t(:st_bt_powered_on)
    when "powered_off" then FmrbI18n.t(:st_bt_powered_off)
    else ""
    end
  end
end

begin
  app = LinuxSettingsApp.new
  app.start
rescue => e
  Log.error("LinuxSettings: #{e}")
end
