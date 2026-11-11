# The Wi-Fi page (doc/wifi_settings): one page, two homes.
#
#   - the PC's settings window (native/apps/linux_settings.app.rb, its
#     "Wi-Fi" tab; NetworkManager behind FmrbApp.wifi_networks), which takes
#     this file in through its "# combine:" line;
#   - the devices' Wi-Fi window (default/wifi, wifi.app.rb beside this
#     directory; the station's manager task behind the same calls,
#     main/drivers/wifi/wifi_task.c), opened from the desktop's Network
#     dialog.
#
# Both read FmrbApp.wifi_networks: "wifi\t<status>\t<active>\t<busy>\t<code>\t<arg>"
# then "ap\t<ssid>\t<strength>\t<secured>\t<active>\t<saved>[\t<in reach>]"
# per network. Only a device writes the seventh field (0: a saved network
# that the last scan did not see) and the statuses disabled / ble / failed
# (the station did not start) and the code forgotten.
#
# The including class defines ROW_H, ROWS and LIST_Y (read as
# self.class::NAME: a bare constant in a mixin is not found here), sets
# @w/@h to its user area and keeps @aps, @sel, @sel_name, @top, @pw, @msg,
# @wifi_text and @wifi_head. The list helpers here (selection, scroll mark,
# text) serve the PC window's Bluetooth page too.

module WifiPageMixin
  def wifi_strings
    {
      "en" => {
        st_wifi: "Wi-Fi", st_scan: "Scan", st_connect: "Connect", st_disconnect: "Disconnect",
        st_forget: "Forget",
        st_password: "Password:",
        st_wifi_using: "Connected to %s", st_wifi_none_used: "Not connected",
        st_wifi_no_nm: "NetworkManager is not running",
        st_wifi_no_dev: "No Wi-Fi device", st_wifi_off: "Wi-Fi is off (turn it on in Linux)",
        st_wifi_disabled: "Wi-Fi is off (wifi_auto_start, wifi.toml)",
        st_wifi_ble: "Bluetooth has the radio (one at a time here)",
        st_wifi_failed: "Wi-Fi did not start (see the log)",
        st_wifi_starting: "Asking NetworkManager...", st_wifi_empty: "No networks in reach",
        st_scanning: "Scanning...", st_connecting: "Connecting to %s...",
        st_connected: "Connected to %s", st_disconnected: "Disconnected",
        st_need_password: "%s needs its password", st_auth_failed: "%s: the password was not accepted",
        st_no_network: "%s is not in reach", st_enterprise: "%s needs settings made in Linux",
        st_failed: "Could not connect to %s", st_refused: "Refused: %s",
        st_forgotten: "Forgot %s",
        st_type_password: "Type the password, then Connect",
        st_saved: "saved"
      },
      "ja" => {
        st_wifi: "Wi-Fi", st_scan: "探す", st_connect: "つなぐ", st_disconnect: "切る",
        st_forget: "忘れる",
        st_password: "パスワード:",
        st_wifi_using: "%s につながっています", st_wifi_none_used: "つながっていません",
        st_wifi_no_nm: "NetworkManager が動いていません",
        st_wifi_no_dev: "Wi-Fi の装置がありません", st_wifi_off: "Wi-Fi が切れています (Linux で入れる)",
        st_wifi_disabled: "Wi-Fi は切れています (wifi_auto_start・wifi.toml)",
        st_wifi_ble: "電波は Bluetooth が使っています (同時には使えません)",
        st_wifi_failed: "Wi-Fi が始まりませんでした (ログを見る)",
        st_wifi_starting: "NetworkManager に聞いています...", st_wifi_empty: "届くネットワークがありません",
        st_scanning: "探しています...", st_connecting: "%s につないでいます...",
        st_connected: "%s につながりました", st_disconnected: "切りました",
        st_need_password: "%s のパスワードを入れてください", st_auth_failed: "%s: パスワードが違うようです",
        st_no_network: "%s が見つかりません", st_enterprise: "%s は Linux で設定してください",
        st_failed: "%s につなげませんでした", st_refused: "断られました: %s",
        st_forgotten: "%s を忘れました",
        st_type_password: "パスワードを入れて「つなぐ」",
        st_saved: "保存"
      }
    }
  end

  # ---- state

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

  # @aps rows: [ssid, strength, secured, active, saved, in reach].
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
        @aps << [f[1], f[2].to_i, f[3] == "1", f[4] == "1", f[5] == "1", f[6] != "0"]
      end
      i += 1
    end
    keep_selection(@aps, 0)
  end

  def wifi_head(i)
    h = @wifi_head
    (h && h[i]) || ""
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
    rows = self.class::ROWS
    @sel = n - 1 if @sel >= n
    @sel = 0 if @sel < 0
    @top = @sel if @sel < @top
    @top = @sel - rows + 1 if @sel >= @top + rows
    @top = 0 if @top < 0
  end

  def fmt(key, arg)
    FmrbI18n.t(key).sub("%s", arg.to_s)
  end

  def selected_ap
    @aps[@sel]
  end

  # The password field shows for a secured network that is not saved, and
  # after an attempt said the password was wanted or wrong.
  def wifi_needs_password?
    ap = selected_ap
    return false if ap.nil?
    code = wifi_head(4)
    asked = (code == "need_password" || code == "auth_failed") && wifi_head(5) == ap[0]
    (ap[2] && !ap[4]) || asked
  end

  # Connect: the selected network, with the password typed (cleared from
  # here as the attempt is asked for). false when the password is wanted.
  def wifi_connect_selected
    ap = selected_ap
    return true if ap.nil?
    if wifi_needs_password? && @pw.empty?
      @msg = FmrbI18n.t(:st_type_password)
      return false
    end
    pw = @pw
    @pw = ""
    FmrbApp.wifi_connect(ap[0], pw)
    pw = nil
    @msg = ""
    true
  end

  # A key typed into the password field; true when it was one.
  def wifi_password_key(ch)
    if ch == 8
      @pw = @pw[0, @pw.length - 1] if @pw.length > 0
      return true
    elsif ch >= 32 && ch <= 126 && @pw.length < 63
      @pw = @pw + ch.chr
      return true
    end
    false
  end

  # ---- drawing

  def ax(x)
    @user_area_x0 + x
  end

  def ay(y)
    @user_area_y0 + y
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
    list_y = self.class::LIST_Y
    row_h = self.class::ROW_H
    rows = self.class::ROWS
    st = wifi_head(1)
    line = case st
           when "none" then FmrbI18n.t(:st_wifi_no_nm)
           when "nodevice" then FmrbI18n.t(:st_wifi_no_dev)
           when "off" then FmrbI18n.t(:st_wifi_off)
           when "disabled" then FmrbI18n.t(:st_wifi_disabled)
           when "ble" then FmrbI18n.t(:st_wifi_ble)
           when "failed" then FmrbI18n.t(:st_wifi_failed)
           when "starting" then FmrbI18n.t(:st_wifi_starting)
           else
             wifi_head(2).empty? ? FmrbI18n.t(:st_wifi_none_used) : fmt(:st_wifi_using, wifi_head(2))
           end
    status_line(20, line)
    if st == "ok" && @aps.empty?
      text(8, list_y + 2, FmrbI18n.t(:st_wifi_empty), theme_border)
    end
    i = 0
    while i < rows
      k = @top + i
      break if k >= @aps.size
      draw_ap_row(list_y + i * row_h, @aps[k], k == @sel)
      i += 1
    end
    draw_scroll_mark(@aps.size)
    py = list_y + rows * row_h + 3
    if wifi_needs_password?
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
    busy = wifi_head(3)
    return fmt(:st_connecting, wifi_head(5)) if busy == "connecting"
    return FmrbI18n.t(:st_scanning) if busy == "scanning"
    code = wifi_head(4)
    arg = wifi_head(5)
    case code
    when "connected" then fmt(:st_connected, arg)
    when "disconnected" then FmrbI18n.t(:st_disconnected)
    when "need_password" then fmt(:st_need_password, arg)
    when "auth_failed" then fmt(:st_auth_failed, arg)
    when "no_network" then fmt(:st_no_network, arg)
    when "enterprise" then fmt(:st_enterprise, arg)
    when "failed" then fmt(:st_failed, arg)
    when "refused" then fmt(:st_refused, arg)
    when "forgotten" then fmt(:st_forgotten, arg)
    else ""
    end
  end

  def draw_ap_row(y, ap, selected)
    row_h = self.class::ROW_H
    bg = selected ? theme_accent : theme_bg
    fg = selected ? theme_fg_light : theme_fg
    @gfx.fill_rect(ax(2), ay(y), @w - 12, row_h, bg)
    # the strength: four bars (none for a saved network out of reach)
    bars = ap[5] ? (ap[1] + 12) / 25 : 0
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
    name_fg = ap[5] || selected ? fg : theme_border
    @gfx.draw_text(ax(20), ay(y + 2), fit(name, @w - 50 - rw), name_fg, bg, mixed: true)
    if ap[2]
      # a lock: a small box with a loop
      lx = @w - 20
      @gfx.fill_rect(ax(lx), ay(y + 5), 6, 5, fg)
      @gfx.draw_rect(ax(lx + 1), ay(y + 2), 4, 4, fg)
    end
    @gfx.draw_text(ax(@w - 24 - rw), ay(y + 2), right, fg, bg, mixed: true) unless right.empty?
  end

  def draw_scroll_mark(n)
    rows = self.class::ROWS
    return if n <= rows
    x = @w - 8
    top = self.class::LIST_Y
    h = rows * self.class::ROW_H
    @gfx.fill_rect(ax(x), ay(top), 3, h, theme_border)
    th = h * rows / n
    th = 6 if th < 6
    ty = top + (h - th) * @top / (n - rows)
    @gfx.fill_rect(ax(x), ay(ty), 3, th, theme_fg)
  end

  def draw_message(s)
    return if s.nil? || s.empty?
    text(4, @h - 14, fit(s, @w - 8), theme_accent)
  end
end
