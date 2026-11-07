# Welcome / status screen for the small single-app boards (StickS3 240x135,
# AtomS3R 128x128; their startup_app). Someone who has just written the
# firmware from the Web Installer sees that the board works and how to go on:
#
#   - "Family mruby", the OS version and the board
#   - Wi-Fi: off / connecting (to the SSID) / connected with the IP and the
#     mDNS name (FmrbApp.wifi_info, polled once a second)
#   - what to do next: the Wi-Fi settings are written over BLE from the
#     Family mruby Console after powering on with the button held (the setup
#     mode; with no Wi-Fi settings at all the board starts in it by itself,
#     and the kernel shows the BLE name there -- this app does not run then)
#   - a square going round in the title bar and the uptime: a frozen board
#     shows as a still picture
#
# Draws for whatever size it is given (6x8 font; doubled where the screen is
# 200 px wide or more), so it also opens on the desktop. No input.

class WelcomeApp < FmrbApp
  TICK_MS = 250
  WIFI_POLL_MS = 1000
  COL_OK = 0x14       # dark green
  COL_WAIT = 0xE8     # orange

  def on_create
    @t0 = Machine.board_millis
    @phase = 0
    @wifi_at = nil
    @wifi_key = nil
    @secs = -1
    layout(@user_area_width, @user_area_height)
    Log.info("welcome: #{FmrbConst::BOARD} #{@w}x#{@h} v#{FmrbConst::OS_VERSION}")
    redraw
  end

  def on_resize(new_width, new_height)
    layout(new_width, new_height)
    redraw
  end

  def on_update
    now = Machine.board_millis
    changed = false
    if @wifi_at.nil? || now - @wifi_at >= WIFI_POLL_MS
      @wifi_at = now
      changed = poll_wifi
    end
    draw_status if changed
    @phase = (@phase + 1) % 4
    draw_spinner(false)
    draw_uptime(now)
    @gfx.present
    TICK_MS
  end

  private

  # Sizes and rows for the screen. big: the 2x text for the title, the
  # Wi-Fi line and the address.
  def layout(w, h)
    @w = w
    @h = h
    @big = w >= 200
    @x0 = @user_area_x0
    @y0 = @user_area_y0
    @pad = @big ? 4 : 3
    @band_h = @big ? 22 : 14
    @line_h = 10
    @cols = (w - 2 * @pad) / 6
  end

  # Reads Wi-Fi; true when what is shown has to change.
  def poll_wifi
    info = FmrbApp.wifi_info
    if info.nil?
      st = :off
      ip = ""
      ssid = ""
      host = ""
    else
      ip = info[:ip].to_s
      ssid = info[:ssid].to_s
      host = info[:hostname].to_s
      if info[:connected]
        st = :connected
      elsif ssid.length > 0
        st = :connecting
      else
        st = :off
      end
    end
    key = "#{st}|#{ip}|#{ssid}|#{host}"
    return false if key == @wifi_key
    @wifi_key = key
    @wifi_state = st
    @ip = ip
    @ssid = ssid
    @host = host
    if st == :connected
      Log.info("welcome: Wi-Fi connected #{ip} #{host.length > 0 ? host + '.local' : ''}")
    else
      Log.info("welcome: Wi-Fi #{st} #{ssid}")
    end
    true
  end

  def board_name(short)
    b = FmrbConst::BOARD
    maker = short ? "" : "M5Stack "
    return "#{maker}StickS3" if b == "sticks3"
    return "#{maker}AtomS3R" if b == "atoms3r"
    return "#{maker}Tab5" if b == "tab5"
    b
  end

  # How to enter the setup mode on this board (kernel: FMRB_PIN_BUTTON_ENTER
  # held at power-on).
  def hold_phrase(cap)
    b = FmrbConst::BOARD
    po = cap ? "Power on" : "power on"
    return "#{po} holding the front button" if b == "sticks3"
    return "#{po} with the screen pressed" if b == "atoms3r"
    "#{po} holding the button"
  end

  def redraw
    clear_user_area
    @gfx.set_font(:default)
    # Title band
    @gfx.fill_rect(@x0, @y0, @w, @band_h, theme_border)
    @gfx.set_text_size(@big ? 2 : 1)
    @gfx.draw_text(@x0 + @pad, @y0 + 3, "Family mruby", theme_fg_light)
    @gfx.set_text_size(1)
    # Version and board, shortened to what fits
    vb = "v#{FmrbConst::OS_VERSION}  #{board_name(false)}"
    vb = "v#{FmrbConst::OS_VERSION} #{board_name(true)}" if vb.length > @cols
    @gfx.draw_text(@x0 + @pad, @y0 + @band_h + 3, vb[0, @cols], theme_fg)
    # The block is drawn whether or not Wi-Fi changed (the screen was just
    # cleared); the poll logs only a change, so a resize does not log again.
    @wifi_at = Machine.board_millis
    poll_wifi
    draw_status
    @secs = -1
    draw_spinner(true)
    draw_uptime(Machine.board_millis)
    @gfx.present
  end

  # Wi-Fi block and the next-step hint: from under the version line to above
  # the uptime row.
  def draw_status
    top = @y0 + @band_h + 3 + @line_h + 3
    bottom = @y0 + @h - @line_h - 2
    @gfx.fill_rect(@x0, top, @w, bottom - top, theme_bg)
    x = @x0 + @pad
    y = top
    st = @wifi_state
    if st == :connected
      label = "Wi-Fi: connected"
      col = COL_OK
    elsif st == :connecting
      label = "Wi-Fi: connecting"
      col = COL_WAIT
    else
      label = "Wi-Fi: off"
      col = theme_border
    end
    big_ok = @big && label.length * 12 <= @w - 2 * @pad
    @gfx.set_text_size(big_ok ? 2 : 1)
    @gfx.draw_text(x, y, label, col)
    y += big_ok ? 18 : @line_h
    @gfx.set_text_size(1)
    if st == :connected
      if @big && @ip.length * 12 <= @w - 2 * @pad
        @gfx.set_text_size(2)
        @gfx.draw_text(x, y, @ip, theme_fg)
        @gfx.set_text_size(1)
        y += 18
      else
        @gfx.draw_text(x, y, @ip[0, @cols], theme_fg)
        y += @line_h
      end
      if @host.length > 0
        @gfx.draw_text(x, y, "#{@host}.local"[0, @cols], theme_fg)
        y += @line_h
      end
      hint = "To change Wi-Fi: #{hold_phrase(false)}, then use Family mruby Console (BLE)."
    elsif st == :connecting
      @gfx.draw_text(x, y, "to #{@ssid}"[0, @cols], theme_fg)
      y += @line_h
      hint = "Not joining? #{hold_phrase(true)}, then open Family mruby Console in a browser to set Wi-Fi over BLE."
    else
      hint = "#{hold_phrase(true)}, then open Family mruby Console in a browser to set Wi-Fi over BLE."
    end
    y += 4
    draw_wrapped(x, y, hint, bottom, theme_border)
  end

  # Word wrap at @cols columns of the 6x8 font, down to the row above bottom.
  def draw_wrapped(x, y, text, bottom, color)
    words = text.split(" ")
    line = ""
    i = 0
    n = words.size
    while i < n
      w = words[i]
      cand = line.length == 0 ? w : "#{line} #{w}"
      if cand.length > @cols && line.length > 0
        return if y + 8 > bottom
        @gfx.draw_text(x, y, line, color)
        y += @line_h
        line = w
      else
        line = cand
      end
      i += 1
    end
    @gfx.draw_text(x, y, line, color) if line.length > 0 && y + 8 <= bottom
  end

  # Four squares in a 2x2 block at the right of the title band; one is lit
  # and it goes round clockwise every tick. all: draw the four (after a
  # clear), else only the one that went out and the one that lit.
  def draw_spinner(all)
    k = 0
    while k < 4
      prev = (@phase + 3) % 4
      draw_square(k, k == @phase) if all || k == @phase || k == prev
      k += 1
    end
  end

  def draw_square(k, lit)
    s = @big ? 6 : 4
    gap = 1
    bx = @x0 + @w - @pad - 2 * s - gap
    by = @y0 + (@band_h - 2 * s - gap) / 2
    cx = bx + (k == 1 || k == 2 ? s + gap : 0)
    cy = by + (k >= 2 ? s + gap : 0)
    @gfx.fill_rect(cx, cy, s, s, lit ? theme_fg_light : theme_border)
    @gfx.draw_rect(cx, cy, s, s, theme_fg_light) unless lit
  end

  # "up m:ss" (h:mm:ss after an hour) at the bottom right, when it changes.
  def draw_uptime(now)
    secs = (now - @t0) / 1000
    return if secs == @secs
    @secs = secs
    m = secs / 60
    s = secs % 60
    t = if m >= 60
          "up #{m / 60}:#{pad2(m % 60)}:#{pad2(s)}"
        else
          "up #{m}:#{pad2(s)}"
        end
    y = @y0 + @h - @line_h
    @gfx.fill_rect(@x0, y - 1, @w, @line_h + 1, theme_bg)
    @gfx.set_text_size(1)
    @gfx.draw_text(@x0 + @w - @pad - 6 * t.length, y, t, theme_border)
  end

  def pad2(n)
    n < 10 ? "0#{n}" : n.to_s
  end
end

begin
  app = WelcomeApp.new
  app.start
rescue => e
  Log.error("welcome: #{e.message}")
end
