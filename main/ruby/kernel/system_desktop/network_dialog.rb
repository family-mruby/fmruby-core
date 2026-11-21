# Network info dialog for SystemDesktopApp (Modern / ESP32-P4 and Linux).
#
# Read-only status view of the network: connection state, associated
# access point (SSID, Modern only), acquired IPv4 address, and the mDNS
# hostname used for the on-device web console (http://<hostname>.local/).
#
# The data comes from FmrbApp.wifi_info: WiFi STA state on Modern, host
# network state on the Linux dev build, nil on Retro (icon hidden, and the
# "Network" menu entry is only wired in on Modern via
# FmrbConst::CHIP_MODEL == "ESP32-P4"); net_refresh still guards against a
# nil result defensively.
#
# While open, on_update re-reads wifi_info every ~1s so the IP appears as
# soon as DHCP completes without the user reopening the dialog.
#
# Where Family mruby can choose the network (FmrbSystem.wifi_networks is not
# ""), a third button opens the Wi-Fi page (the networks in reach,
# connecting, disconnecting): on the PC (L14) the settings window's, on a
# device with Wi-Fi the built-in default/wifi window, the same page
# (doc/wifi_settings). The simulation and the browser have no such button.
# The device's Wi-Fi window is one: pressed again while it is open, the
# button brings that window to the front instead of opening a second.
#
# In the browser build a third line says whether the router checks this
# page's name (asterism-console's door connected it with its user's
# certificate, doc/asterism_file_transfer/report/t8.md): the page writes the
# node ID to DOOR_FILE while that holds (web/main.js), the file the transfer
# app reads too. Re-read with the rest every ~1s while the dialog is open.
#
# Global IP (doc/global_ip): where system_conf names a lookup URL
# (`global_ip_url`, a top-level string), the dialog grows by two lines and a
# Look up button, and each press of the button asks that URL once which
# address the request came from (opening the dialog asks nothing). The
# answer must look like one IPv4 or IPv6 address (net_gip_tick); anything
# else (an HTML page, several lines, a long body, a non-2xx status) is shown
# as a failure.
# Without the key the lines are not drawn at all -- no lookup service is
# built in. The request goes through FmrbNet.request and is collected from
# the ~1s tick: in the browser it runs on the page while the desktop keeps
# going; on a board, the simulation and the native program it is made inside
# `request` and holds the desktop's own task until the server answers, so the
# press draws "asking" first and the lookup starts on the next tick. A press
# while one is asked, or while not connected, does nothing. The answer lives
# only in the dialog's own variables and is not logged.

module NetworkDialogMixin
  NET_W       = 240
  NET_H       = 104
  NET_TITLE_H = 14
  NET_BG      = FmrbConst::THEME_WINDOW_BG
  NET_TEXT    = FmrbConst::THEME_TEXT
  NET_HEADER  = FmrbConst::THEME_MENU_BG
  NET_BTN     = FmrbConst::THEME_BUTTON
  NET_BORDER  = FmrbConst::THEME_BORDER
  NET_OK      = 0x1C  # green-ish: connected
  NET_WARN    = 0xE0  # red-ish: not connected
  NET_DOOR_FILE = "/tmp/.asterism_door"
  NET_WIFI_APP  = "default/wifi"
  NET_WIFI_NAME = "Wi-Fi"   # its name in the process list (fmrb_app_spawner.c)

  # ---- Lifecycle ----

  def open_network_dialog
    @net_open = true
    @net_info = nil
    @net_door = ""
    net_gip_reset
    url = read_conf_string("global_ip_url")
    url = url.strip if url
    if url && !url.empty?
      @net_gip_url = url
      # Two more lines (label with the Look up button, then the address on
      # its own so a full IPv6 address, 39 chars, fits), and a little wider.
      # "-" until the button is pressed.
      @net_gip = "-"
      @net_w = NET_W + 16
      @net_h = NET_H + 32
    end
    show_network_widgets(true)
    @net_x = (@window_width - @net_w) / 2
    @net_y = (@window_height - @net_h) / 2
    menu_h = self.class::MENU_BAR_HEIGHT
    @net_y = menu_h + 2 if @net_y < menu_h + 2
    net_refresh
    close_launcher
    close_dropdown
    notify_overlay_state(true, @net_x, @net_y, @net_w, @net_h)
    update_composite_regions
    draw_foreground
  end

  def close_network_dialog
    return unless @net_open
    @net_open = false
    net_gip_reset
    show_network_widgets(false)
    @ui.flush
    notify_overlay_after_dialog
    update_composite_regions
    draw_foreground
  end

  def hit_network_dialog?(x, y)
    @net_open &&
      x >= @net_x && x < @net_x + @net_w &&
      y >= @net_y && y < @net_y + @net_h
  end

  def net_refresh
    @net_door = net_read_door if self.class::ON_WEB
    @net_info = FmrbApp.wifi_info
  rescue => e
    Log.error("Network: wifi_info failed: #{e.message}")
    @net_info = nil
  end

  # ---- Global IP ----

  # Forget the lookup: drop any request in flight and the answer with it, and
  # give the dialog its plain size back.
  def net_gip_reset
    @net_gip_req.cancel if @net_gip_req
    @net_gip_req = nil
    @net_gip_url = nil
    @net_gip = nil
    @net_w = NET_W
    @net_h = NET_H
    nil
  end

  # From the ~1s tick. Collect an answer, or start the lookup the button
  # asked for (it put up "asking", drawn before this tick holds the task).
  #
  # The answer is taken only as a single line of an address's characters
  # (hex digits, ':' and '.', at most 45 bytes: IPv6 text in its longest,
  # IPv4-mapped form) with either three dots or at least two colons. It is a
  # shape check, not a full parse, and it is enough to turn away an HTML
  # page, an error text or several lines.
  def net_gip_tick
    req = @net_gip_req
    if req
      return unless req.done?
      @net_gip_req = nil
      body = req.ok? ? req.body.to_s.strip : ""
      n = body.bytesize
      ok = n >= 2 && n <= 45
      dots = 0
      colons = 0
      i = 0
      while ok && i < n
        b = body.getbyte(i)
        if b == 0x2E
          dots += 1
        elsif b == 0x3A
          colons += 1
        else
          ok = (b >= 0x30 && b <= 0x39) || (b >= 0x41 && b <= 0x46) || (b >= 0x61 && b <= 0x66)
        end
        i += 1
      end
      ok = ok && ((colons == 0 && dots == 3) || colons >= 2)
      @net_gip = ok ? body : (FmrbI18n.lang == "ja" ? "調べられませんでした" : "Could not find out")
      return
    end
    url = @net_gip_url
    return unless url && @net_gip == (FmrbI18n.lang == "ja" ? "調べています..." : "Asking...")
    @net_gip_req = FmrbNet.request(url)
    nil
  end


  # The label, then the answer on its own line.
  #
  # The three texts of these lines are written here rather than registered in
  # i18n.rb: the desktop's table holds exactly 128 words per language, and the
  # 129th makes both hashes grow to their next size, several KB of the
  # desktop's VM pool for good (doc/global_ip/report/p1.md).
  def net_draw_gip(x, y)
    label = FmrbI18n.lang == "ja" ? "グローバル IP:" : "Global IP:"
    @gfx.draw_text(x + 8, y, label, NET_TEXT, NET_BG, mixed: true)
    gw = @ui.find(:net_gip).w
    @ui.move(:net_gip, x + @net_w - gw - 8, y - 2, gw, NET_GIP_BTN_H)
    value = FmrbI18n.truncate_to(@net_gip, @net_w - 22)
    @gfx.draw_text(x + 14, y + 16, value, NET_TEXT, NET_BG, mixed: true)
  end

  # The node the router checks for this page, or "" (web/main.js writes it).
  def net_read_door
    return "" unless File.exist?(NET_DOOR_FILE)
    f = File.open(NET_DOOR_FILE, "r")
    text = f.read.to_s
    f.close
    text.strip
  rescue
    ""
  end

  # ---- Drawing ----

  def draw_network_dialog
    return unless @net_open
    x = @net_x
    y = @net_y
    w = @net_w
    @gfx.fill_rect(x, y, w, @net_h, NET_BG)
    @gfx.draw_rect(x, y, w, @net_h, NET_BORDER)
    @gfx.fill_rect(x + 1, y + 1, w - 2, NET_TITLE_H - 1, NET_HEADER)
    @gfx.draw_text(x + 4, y + 3, FmrbI18n.t(:network), FmrbConst::THEME_TEXT_LIGHT, NET_HEADER, mixed: true)

    body_y = y + NET_TITLE_H + 6
    if self.class::ON_WEB
      # The page has no network of its own to report -- the loopback address
      # the host port hands back would only invite a pointless Refresh. The
      # entry stays because this is where the browser's own connections
      # (HTTP, WebSocket) will be described when they arrive.
      @gfx.draw_text(x + 8, body_y, FmrbI18n.t(:net_browser), NET_TEXT, NET_BG, mixed: true)
      @gfx.draw_text(x + 8, body_y + 14, FmrbI18n.t(:net_browser2), NET_TEXT, NET_BG, mixed: true)
      door = @net_door
      if door != ""
        label = FmrbI18n.t(:net_door_ok)
        sw = FmrbI18n.text_width(label) + 10
        @gfx.fill_rect(x + 8, body_y + 31, sw, 12, NET_OK)
        @gfx.draw_text(x + 13, body_y + 33, label, FmrbGfx::BLACK, NET_OK, mixed: true)
        @gfx.draw_text(x + 8 + sw + 6, body_y + 33, FmrbI18n.truncate_to(door, w - sw - 30), NET_TEXT, NET_BG,
                       mixed: true)
      else
        @gfx.draw_text(x + 8, body_y + 33, FmrbI18n.t(:net_door_none), NET_TEXT, NET_BG, mixed: true)
      end
      net_draw_gip(x, body_y + 47) if @net_gip
    elsif @net_info.nil?
      @gfx.draw_text(x + 8, body_y, FmrbI18n.t(:wifi_unavailable), NET_TEXT, NET_BG, mixed: true)
    else
      connected = @net_info[:connected]
      status = connected ? FmrbI18n.t(:connected) : FmrbI18n.t(:disconnected)
      status_bg = connected ? NET_OK : NET_WARN
      # Status pill so the connected/disconnected state reads at a glance.
      sw = FmrbI18n.text_width(status) + 10
      @gfx.fill_rect(x + 8, body_y - 1, sw, 12, status_bg)
      # Black on the green pill: white vanished into the light green.
      @gfx.draw_text(x + 13, body_y + 1, status, FmrbGfx::BLACK, status_bg, mixed: true)

      ssid = @net_info[:ssid]
      ssid = "-" if ssid.nil? || ssid.empty?
      # The signal, where the OS tells it (the native program, L6).
      signal = @net_info[:signal]
      ssid = "#{ssid} (#{signal}%)" if signal
      ip = @net_info[:ip] || "-"
      host = @net_info[:hostname]

      row_y = body_y + 16
      net_draw_row(x, row_y,      FmrbI18n.t(:access_point), ssid)
      net_draw_row(x, row_y + 14, FmrbI18n.t(:ip_address),   ip)
      host_y = row_y + 28
      if @net_gip
        net_draw_gip(x, host_y)
        host_y += 32
      end
      if host && !host.empty?
        net_draw_row(x, host_y, FmrbI18n.t(:hostname_label), "#{host}.local")
      end
    end

    net_draw_buttons
  end

  def net_draw_row(x, y, label, value)
    @gfx.draw_text(x + 8, y, "#{label}:", NET_TEXT, NET_BG, mixed: true)
    value = FmrbI18n.truncate_to(value, @net_w - 70)
    @gfx.draw_text(x + 62, y, value, NET_TEXT, NET_BG, mixed: true)
  end

  NET_BTN_H = 16
  NET_GIP_BTN_H = 14

  def build_network_widgets
    lr = FmrbI18n.t(:refresh)
    lc = FmrbI18n.t(:close)
    @ui.button(:net_refresh, 0, 0, FmrbI18n.text_width(lr) + 12, NET_BTN_H, lr)
    @ui.button(:net_close, 0, 0, FmrbI18n.text_width(lc) + 12, NET_BTN_H, lc)
    # Global IP's Look up, beside its label (shown only with global_ip_url).
    lg = FmrbI18n.lang == "ja" ? "調べる" : "Look up"
    @ui.button(:net_gip, 0, 0, FmrbI18n.text_width(lg) + 12, NET_GIP_BTN_H, lg)
    @net_settings_ok = FmrbSystem.wifi_networks != ""
    if @net_settings_ok
      lw = FmrbI18n.t(:wifi_settings)
      @ui.button(:net_wifi, 0, 0, FmrbI18n.text_width(lw) + 12, NET_BTN_H, lw)
    end
    show_network_widgets(false)
    nil
  end

  def show_network_widgets(on)
    @ui.set_visible(:net_refresh, on && !self.class::ON_WEB)
    @ui.set_visible(:net_close, on)
    @ui.set_visible(:net_gip, on && !@net_gip_url.nil?)
    @ui.set_visible(:net_wifi, on) if @net_settings_ok
    nil
  end

  def net_draw_buttons
    btn_y = @net_y + @net_h - 22
    rw = @ui.find(:net_refresh).w
    cw = @ui.find(:net_close).w
    ww = @net_settings_ok ? @ui.find(:net_wifi).w + 8 : 0
    bx = @net_x + (@net_w - (rw + 8 + ww + cw)) / 2
    @ui.move(:net_refresh, bx, btn_y, rw, NET_BTN_H)
    @ui.move(:net_wifi, bx + rw + 8, btn_y, ww - 8, NET_BTN_H) if @net_settings_ok
    @ui.move(:net_close, bx + rw + 8 + ww, btn_y, cw, NET_BTN_H)
    @ui.invalidate_all
  end

  # ---- Interaction ----

  def handle_network_dialog_widget(id)
    case id
    when :net_refresh
      net_refresh
      draw_foreground
    when :net_close then close_network_dialog
    when :net_gip
      # One lookup at a time, and only while connected (the browser has no
      # state to show, so it always may). "asking" is drawn now; the next tick
      # starts the request, which on a board holds the desktop until answered.
      info = @net_info
      if @net_gip_url && !@net_gip_req && (self.class::ON_WEB || (info && info[:connected]))
        # A second press before the tick sets the same text: still one lookup.
        @net_gip = FmrbI18n.lang == "ja" ? "調べています..." : "Asking..."
        draw_foreground
      end
    when :net_wifi
      close_network_dialog
      # FmrbSystem.linux_settings exists on the PC only (a device must not have
      # it: the checkpoint tests name it as missing there).
      if FmrbConst::PLATFORM == "esp32"
        net_open_wifi_window
      else
        FmrbSystem.linux_settings("wifi")
      end
    end
    nil
  end

  # The Wi-Fi window to the front if it is open, else open it.
  def net_open_wifi_window
    pid = net_wifi_window_pid
    if pid >= 0
      Log.info("Network: Wi-Fi window already open (pid=#{pid}), bringing it to the front")
      @taskbar_focused_pid = pid
      send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
                   { "cmd" => "focus_app", "pid" => pid })
    else
      spawn_app(NET_WIFI_APP)
    end
    nil
  end

  # The pid of the open Wi-Fi window, -1 when there is none (starting,
  # running or suspended count, as in the taskbar).
  def net_wifi_window_pid
    procs = FmrbApp.ps
    i = 0
    while i < procs.size
      pr = procs[i]
      st = pr[:state]
      if st >= 1 && st <= 3 && pr[:name] == NET_WIFI_NAME
        return pr[:id]
      end
      i += 1
    end
    -1
  end

  # Periodic tick from on_update: refresh so a late DHCP lease shows up while
  # the dialog stays open. The caller repaints after this returns.
  def tick_network_dialog
    return unless @net_open
    net_refresh
    net_gip_tick if @net_gip_url
  end
end
