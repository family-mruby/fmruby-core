# The devices' Wi-Fi window (doc/wifi_settings): the PC settings window's
# Wi-Fi page (WifiPageMixin, wifi/wifi_page.rb, put in front of this file at
# build time), opened from the desktop's Network dialog ("Wi-Fi...").
#
# The networks in reach, strongest first (the one in use at the top), then
# the saved ones the last scan did not see. Choose one and Connect; a
# secured network that is not saved asks for its password, typed on the
# keyboard (shown as stars) and cleared from here as the attempt is asked
# for. The station's manager (main/drivers/wifi/wifi_task.c) saves the
# network in /home/wifi.toml once it gets an address (up to five, newest
# first) and goes back to the saved ones when it does not. Disconnect
# leaves it off until a network is chosen again (or the next boot); Forget
# drops the selected saved network from the file. Scan asks for a fresh
# look; one is asked for when the window opens.
#
# Everything here only asks (FmrbApp.wifi_*): the manager task does the
# work and this window reads the outcome about once a second, so a scan or
# a connection never holds up drawing or input.

class WifiSettingsApp < FmrbApp
  include WifiPageMixin

  ROW_H = 12
  ROWS = 7
  LIST_Y = 32
  POLL_MS = 1000

  def on_create
    FmrbI18n.add(wifi_strings)
    @gfx.set_font(:default)
    @gfx.set_text_size(1)
    @w = @user_area_width
    @h = @user_area_height
    @aps = []
    @sel = 0
    @sel_name = nil
    @top = 0
    @pw = ""
    @msg = ""
    @wifi_text = nil
    @ip = ""
    build_widgets
    Log.info("wifi: user area #{@w}x#{@h} at (#{@user_area_x0},#{@user_area_y0})")
    # The list is only as fresh as the last scan: ask for one on opening.
    @wifi_text = FmrbApp.wifi_networks(true)
    parse_wifi
    log_state("wifi", @wifi_head, @aps)
    read_ip
    draw_page
  end

  def on_update
    changed = read_state
    changed = true if read_ip
    draw_page if changed
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

  # Four buttons in a row under the list, the window's width shared out.
  def build_widgets
    @ui = FmrbUI.new(self)
    by = @h - 34
    gap = 4
    bw = (@w - 8 - gap * 3) / 4
    @ui.button(:connect, 4, by, bw, 16, FmrbI18n.t(:st_connect))
    @ui.button(:disconnect, 4 + (bw + gap), by, bw, 16, FmrbI18n.t(:st_disconnect))
    @ui.button(:scan, 4 + (bw + gap) * 2, by, bw, 16, FmrbI18n.t(:st_scan))
    @ui.button(:forget, 4 + (bw + gap) * 3, by, bw, 16, FmrbI18n.t(:st_forget))
  end

  def act(id)
    case id
    when :scan
      @wifi_text = FmrbApp.wifi_networks(true)
      parse_wifi
      draw_page
    when :connect then do_connect
    when :disconnect
      FmrbApp.wifi_disconnect
      read_state
      draw_page
    when :forget then do_forget
    end
  end

  # true when the text changed.
  def read_state
    t = FmrbApp.wifi_networks(false)
    return false if t == @wifi_text
    @wifi_text = t
    parse_wifi
    log_state("wifi", @wifi_head, @aps)
    true
  end

  # The address, shown above the list; true when it changed.
  def read_ip
    info = FmrbApp.wifi_info
    ip = info && info[:connected] ? info[:ip].to_s : ""
    return false if ip == @ip
    @ip = ip
    true
  end

  def do_connect
    return if selected_ap.nil?
    unless wifi_connect_selected
      draw_page
      return
    end
    read_state
    draw_page
  end

  def do_forget
    ap = selected_ap
    return if ap.nil? || !ap[4]
    @msg = ""
    FmrbApp.wifi_forget(ap[0])
    read_state
    draw_page
  end

  def click_list(x, y)
    return if y < LIST_Y || y >= LIST_Y + ROWS * ROW_H
    i = @top + (y - LIST_Y) / ROW_H
    return if i >= @aps.size
    select(i)
  end

  def select(i)
    return if i == @sel
    @sel = i
    @pw = ""
    @msg = ""
    fix_selection(@aps.size)
    r = @aps[@sel]
    @sel_name = r ? r[0] : nil
    draw_page
  end

  def scroll(d)
    n = @aps.size
    t = @top + d
    t = n - ROWS if t > n - ROWS
    t = 0 if t < 0
    return if t == @top
    @top = t
    draw_page
  end

  def key(sc, ch)
    n = @aps.size
    if sc == FmrbConst::KEY_UP
      select(@sel - 1) if @sel > 0
    elsif sc == FmrbConst::KEY_DOWN
      select(@sel + 1) if @sel + 1 < n
    elsif ch == 13 || ch == 10
      do_connect
    elsif wifi_needs_password?
      draw_page if wifi_password_key(ch)
    end
  end

  def draw_page
    clear_user_area
    text(4, 5, fit(@ip.empty? ? FmrbI18n.t(:st_wifi) : "#{FmrbI18n.t(:st_wifi)}  #{@ip}", @w - 8), theme_border)
    draw_wifi
    @ui.set_visible(:forget, !selected_ap.nil? && selected_ap[4])
    @ui.invalidate_all
    @ui.flush
  end
end

begin
  app = WifiSettingsApp.new
  app.start
rescue => e
  Log.error("WifiSettings: #{e}")
end
