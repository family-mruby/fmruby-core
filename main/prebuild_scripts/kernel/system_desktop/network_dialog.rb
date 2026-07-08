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

  # ---- Lifecycle ----

  def open_network_dialog
    @net_open = true
    @net_info = nil
    @net_close_btn_rect = nil
    @net_x = (@window_width - NET_W) / 2
    @net_y = (@window_height - NET_H) / 2
    menu_h = self.class::MENU_BAR_HEIGHT
    @net_y = menu_h + 2 if @net_y < menu_h + 2
    net_refresh
    close_launcher
    close_dropdown
    notify_overlay_state(true, @net_x, @net_y, NET_W, NET_H)
    update_composite_regions
    draw_foreground
  end

  def close_network_dialog
    return unless @net_open
    @net_open = false
    notify_overlay_state(false, 0, 0, 0, 0)
    update_composite_regions
    draw_foreground
  end

  def hit_network_dialog?(x, y)
    @net_open &&
      x >= @net_x && x < @net_x + NET_W &&
      y >= @net_y && y < @net_y + NET_H
  end

  def net_refresh
    @net_info = FmrbApp.wifi_info
  rescue => e
    Log.error("Network: wifi_info failed: #{e.message}")
    @net_info = nil
  end

  # ---- Drawing ----

  def draw_network_dialog
    return unless @net_open
    x = @net_x
    y = @net_y
    @gfx.fill_rect(x, y, NET_W, NET_H, NET_BG)
    @gfx.draw_rect(x, y, NET_W, NET_H, NET_BORDER)
    @gfx.fill_rect(x + 1, y + 1, NET_W - 2, NET_TITLE_H - 1, NET_HEADER)
    @gfx.draw_text(x + 4, y + 3, FmrbI18n.t(:network), FmrbGfx::WHITE, NET_HEADER, mixed: true)

    body_y = y + NET_TITLE_H + 6
    if @net_info.nil?
      @gfx.draw_text(x + 8, body_y, FmrbI18n.t(:wifi_unavailable), NET_TEXT, NET_BG, mixed: true)
    else
      connected = @net_info[:connected]
      status = connected ? FmrbI18n.t(:connected) : FmrbI18n.t(:disconnected)
      status_bg = connected ? NET_OK : NET_WARN
      # Status pill so the connected/disconnected state reads at a glance.
      sw = FmrbI18n.text_width(status) + 10
      @gfx.fill_rect(x + 8, body_y - 1, sw, 12, status_bg)
      @gfx.draw_text(x + 13, body_y + 1, status, FmrbGfx::WHITE, status_bg, mixed: true)

      ssid = @net_info[:ssid]
      ssid = "-" if ssid.nil? || ssid.empty?
      ip = @net_info[:ip] || "-"
      host = @net_info[:hostname]

      row_y = body_y + 16
      net_draw_row(x, row_y,      FmrbI18n.t(:access_point), ssid)
      net_draw_row(x, row_y + 14, FmrbI18n.t(:ip_address),   ip)
      if host && !host.empty?
        net_draw_row(x, row_y + 28, FmrbI18n.t(:hostname_label), "#{host}.local")
      end
    end

    net_draw_buttons
  end

  def net_draw_row(x, y, label, value)
    @gfx.draw_text(x + 8, y, "#{label}:", NET_TEXT, NET_BG, mixed: true)
    value = FmrbI18n.truncate_to(value, NET_W - 70)
    @gfx.draw_text(x + 62, y, value, NET_TEXT, NET_BG, mixed: true)
  end

  def net_draw_buttons
    btn_y = @net_y + NET_H - 22
    btn_h = 16

    label_refresh = FmrbI18n.t(:refresh)
    label_close   = FmrbI18n.t(:close)
    bw_refresh = FmrbI18n.text_width(label_refresh) + 12
    bw_close   = FmrbI18n.text_width(label_close) + 12

    total = bw_refresh + 8 + bw_close
    bx = @net_x + (NET_W - total) / 2

    @net_refresh_btn_rect = [bx, btn_y, bw_refresh, btn_h]
    @gfx.fill_rect(bx, btn_y, bw_refresh, btn_h, NET_BTN)
    @gfx.draw_text(bx + 6, btn_y + 4, label_refresh, FmrbGfx::WHITE, NET_BTN, mixed: true)
    bx += bw_refresh + 8

    @net_close_btn_rect = [bx, btn_y, bw_close, btn_h]
    @gfx.fill_rect(bx, btn_y, bw_close, btn_h, NET_BTN)
    @gfx.draw_text(bx + 6, btn_y + 4, label_close, FmrbGfx::WHITE, NET_BTN, mixed: true)
  end

  # ---- Interaction ----

  def handle_network_dialog_click(x, y)
    if net_hit_rect?(x, y, @net_refresh_btn_rect)
      net_refresh
      draw_foreground
      return
    end
    if net_hit_rect?(x, y, @net_close_btn_rect)
      close_network_dialog
      return
    end
  end

  def net_hit_rect?(x, y, r)
    return false unless r
    x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]
  end

  # Periodic tick from on_update: refresh so a late DHCP lease shows up while
  # the dialog stays open. The caller repaints after this returns.
  def tick_network_dialog
    return unless @net_open
    net_refresh
  end
end
