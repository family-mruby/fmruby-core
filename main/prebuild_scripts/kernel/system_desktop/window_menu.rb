# The window menu (doc/window_menu) for SystemDesktopApp.
#
# A right click on a window's title bar, or a right click or a long press on
# a frameless window, makes the app's base ask the kernel ("window_menu"),
# which sends it here with what the menu needs ("window_menu" {pid, name,
# x, y, movable, reload, send, why}). The menu is drawn on the foreground
# canvas, over every window, like the confirm dialog:
#
#   Send to >   only for an app that can be moved (movable); opens the list
#               of the machines on the network beside it
#   Reload      only for an app started from a file: the "Reload <app>?"
#               question, as the title bar's right click asked before
#   Close       the app's quit request (on_quit_request: closes unless the
#               app asks first)
#
# Send to (M2): with sending on and the transfer app connected (send 2), the
# kernel has the transfer app send its list here at once ("wm_peers" {for,
# n, seen, verified}, then "wm_peer" {for, node, name, host, ok}). A machine
# that does not take apps from here is greyed; clicking it says why.
# Clicking one that may does what the shell's amove does (FmrbApp.move), and
# the answer ("checkpoint_result") is shown for a moment on a plate under
# the menu bar. With sending off (send 0) or not connected yet (send 1),
# Send to is greyed and its list says why.
#
# A click outside closes it (the kernel's overlay rectangle covers the menu
# and its list), as Esc does.

module WindowMenuMixin
  WM_ITEM_H = 12
  WM_W = 84
  WM_SUB_MIN_W = 110
  WM_PAD = 6
  WM_BG = FmrbConst::THEME_WINDOW_BG
  WM_TEXT = FmrbConst::THEME_TEXT
  WM_GREY = FmrbConst::THEME_BORDER
  WM_HIGHLIGHT = FmrbConst::THEME_HIGHLIGHT
  # The plate with what came of a send, under the menu bar
  WM_NOTE_H = 26
  WM_NOTE_Y = 19
  WM_NOTE_MS = 5000
  WM_NOTE_FAIL_MS = 8000
  # a move not answered by then is no longer waited for here
  WM_MOVE_WAIT_MS = 60000

  def setup_window_menu
    @wm_open = false
    @wm_pid = -1
    @wm_name = ""
    @wm_x = 0
    @wm_y = 0
    @wm_items = []
    @wm_send = 0
    @wm_why = ""
    @wm_sub = false
    @wm_sub_x = 0
    @wm_sub_y = 0
    @wm_sub_w = 0
    @wm_sub_h = 0
    @wm_peers = []
    @wm_peers_n = -1
    @wm_peers_seen = false
    @wm_verified = false
    @wm_note1 = ""
    @wm_note2 = ""
    @wm_note_until = 0
    @wm_moving_pid = -1
    @wm_moving_name = ""
    nil
  end

  # ---- opening and closing ----

  def open_window_menu(msg)
    pid = (msg["pid"] || -1).to_i
    return nil if pid < 0
    # Another overlay owns the screen (a question, a dialog): no menu over it.
    if desktop_overlay_open? && !@wm_open
      Log.info("Desktop: window menu for PID #{pid} not opened (another overlay is up)")
      return nil
    end
    @wm_pid = pid
    @wm_name = msg["name"].to_s
    items = []
    items << :wm_send if msg["movable"] == true
    items << :wm_reload if msg["reload"] == true
    items << :wm_close
    @wm_items = items
    @wm_send = (msg["send"] || 0).to_i
    @wm_why = msg["why"].to_s
    @wm_sub = false
    @wm_peers = []
    @wm_peers_n = -1
    @wm_peers_seen = false
    @wm_verified = false
    h = wm_h
    x = (msg["x"] || 0).to_i
    y = (msg["y"] || 0).to_i
    x = @window_width - WM_W if x + WM_W > @window_width
    x = 0 if x < 0
    y = @window_height - h if y + h > @window_height
    y = SystemDesktopApp::MENU_BAR_HEIGHT if y < SystemDesktopApp::MENU_BAR_HEIGHT
    @wm_x = x
    @wm_y = y
    @wm_open = true
    Log.info("Desktop: window menu for PID #{pid} (#{@wm_name}) send=#{@wm_send}")
    wm_overlay_changed
    nil
  end

  def close_window_menu
    return nil unless @wm_open
    @wm_open = false
    @wm_sub = false
    notify_overlay_state(false, 0, 0, 0, 0)
    update_composite_regions
    draw_foreground
    nil
  end

  def wm_h
    @wm_items.size * WM_ITEM_H + 2
  end

  # The kernel's rectangle (clicks in it come here) and the composited
  # regions follow the menu and its list.
  def wm_overlay_changed
    x0 = @wm_x
    y0 = @wm_y
    x1 = @wm_x + WM_W
    y1 = @wm_y + wm_h
    if @wm_sub
      x0 = @wm_sub_x if @wm_sub_x < x0
      y0 = @wm_sub_y if @wm_sub_y < y0
      x1 = @wm_sub_x + @wm_sub_w if @wm_sub_x + @wm_sub_w > x1
      y1 = @wm_sub_y + @wm_sub_h if @wm_sub_y + @wm_sub_h > y1
    end
    notify_overlay_state(true, x0, y0, x1 - x0, y1 - y0)
    update_composite_regions
    draw_foreground
    nil
  end

  # update_composite_regions' part.
  def window_menu_regions(regions)
    if @wm_open
      regions << { dst_x: @wm_x, dst_y: @wm_y, w: WM_W, h: wm_h, transparent: false }
      if @wm_sub
        regions << { dst_x: @wm_sub_x, dst_y: @wm_sub_y, w: @wm_sub_w, h: @wm_sub_h, transparent: false }
      end
    end
    if wm_note_shown?
      regions << { dst_x: wm_note_x, dst_y: WM_NOTE_Y, w: wm_note_w, h: WM_NOTE_H, transparent: false }
    end
    nil
  end

  # ---- the list of the machines (Send to) ----

  # The rows of the list: [text, right text, greyed, what a click does].
  # what: a node to send to, or "" (nothing), or "?<n>" (say why it is
  # greyed: 2 receives nothing, 3 not from here, 4 cannot start apps).
  def wm_rows
    rows = []
    if @wm_send == 0
      rows << [FmrbI18n.t(:wm_send_off), "", true, ""]
      rows << [FmrbI18n.t(:wm_send_off2), "", true, ""]
      return rows
    end
    if @wm_send == 1
      rows << [FmrbI18n.t(:wm_connecting), "", true, ""]
      rows << [@wm_why, "", true, ""] unless @wm_why.empty?
      return rows
    end
    if @wm_peers_n < 0 || (@wm_peers_n == 0 && !@wm_peers_seen)
      rows << [FmrbI18n.t(:wm_looking), "", true, ""]
      return rows
    end
    if @wm_peers.empty?
      rows << [FmrbI18n.t(:wm_no_peers), "", true, ""]
      return rows
    end
    i = 0
    while i < @wm_peers.size
      pe = @wm_peers[i]
      i += 1
      node = pe[:node].to_s
      name = pe[:name].to_s
      label = name.empty? ? node : "#{name} (#{node})"
      ok = pe[:ok].to_i
      grey = ok >= 2
      rows << [label, pe[:host].to_s, grey, grey ? "?#{ok}" : node]
    end
    rows
  end

  def wm_layout_sub
    rows = wm_rows
    w = WM_SUB_MIN_W
    i = 0
    while i < rows.size
      r = rows[i]
      tw = FmrbI18n.text_width(r[0].to_s) + WM_PAD * 2 + 8
      tw += FmrbI18n.text_width(r[1].to_s) + WM_PAD unless r[1].to_s.empty?
      w = tw if tw > w
      i += 1
    end
    w = @window_width - 8 if w > @window_width - 8
    h = rows.size * WM_ITEM_H + 2
    h = @window_height - SystemDesktopApp::MENU_BAR_HEIGHT if h > @window_height - SystemDesktopApp::MENU_BAR_HEIGHT
    # Beside the menu, at the Send to row: to the right, else to the left;
    # where neither has room (a narrow screen), under the menu, so that it
    # does not cover the menu's other rows.
    idx = wm_index(:wm_send)
    y = @wm_y + 1 + (idx < 0 ? 0 : idx) * WM_ITEM_H - 1
    x = @wm_x + WM_W - 1
    if x + w > @window_width
      x = @wm_x - w + 1
      if x < 0
        x = @wm_x
        x = @window_width - w if x + w > @window_width
        x = 0 if x < 0
        y = @wm_y + wm_h - 1
        # no room under it either: above it
        y = @wm_y - h + 1 if y + h > @window_height && @wm_y - h + 1 >= SystemDesktopApp::MENU_BAR_HEIGHT
      end
    end
    y = @window_height - h if y + h > @window_height
    y = SystemDesktopApp::MENU_BAR_HEIGHT if y < SystemDesktopApp::MENU_BAR_HEIGHT
    @wm_sub_x = x
    @wm_sub_y = y
    @wm_sub_w = w
    @wm_sub_h = h
    nil
  end

  def wm_index(key)
    i = 0
    while i < @wm_items.size
      return i if @wm_items[i] == key
      i += 1
    end
    -1
  end

  # "wm_peers" / "wm_peer" from the transfer app, for the menu now open.
  def window_menu_peers(msg)
    return nil unless @wm_open && (msg["for"] || -1).to_i == @wm_pid
    @wm_peers = []
    @wm_peers_n = (msg["n"] || 0).to_i
    @wm_peers_seen = msg["seen"] == true
    @wm_verified = msg["verified"] == true
    wm_list_changed if @wm_peers_n == 0
    nil
  end

  def window_menu_peer(msg)
    return nil unless @wm_open && (msg["for"] || -1).to_i == @wm_pid
    @wm_peers << { node: msg["node"].to_s, name: msg["name"].to_s, host: msg["host"].to_s,
                   ok: (msg["ok"] || 0).to_i }
    wm_list_changed if @wm_peers.size >= @wm_peers_n
    nil
  end

  def wm_list_changed
    return nil unless @wm_sub
    wm_layout_sub
    wm_overlay_changed
    nil
  end

  # ---- drawing ----

  def draw_window_menu
    return nil unless @wm_open
    x = @wm_x
    y = @wm_y
    @gfx.fill_rect(x, y, WM_W, wm_h, WM_BG)
    @gfx.draw_rect(x, y, WM_W, wm_h, FmrbConst::THEME_BORDER)
    i = 0
    while i < @wm_items.size
      key = @wm_items[i]
      iy = y + 1 + i * WM_ITEM_H
      grey = key == :wm_send && @wm_send != 2
      lit = key == :wm_send && @wm_sub
      bg = lit ? WM_HIGHLIGHT : WM_BG
      @gfx.fill_rect(x + 1, iy, WM_W - 2, WM_ITEM_H, bg) if lit
      label = FmrbI18n.t(key)
      @gfx.draw_text(x + WM_PAD, iy + 2, label, grey ? WM_GREY : WM_TEXT, bg, mixed: true)
      @gfx.draw_text(x + WM_W - 10, iy + 2, ">", grey ? WM_GREY : WM_TEXT, bg) if key == :wm_send
      i += 1
    end
    draw_window_menu_sub if @wm_sub
    nil
  end

  def draw_window_menu_sub
    x = @wm_sub_x
    y = @wm_sub_y
    w = @wm_sub_w
    @gfx.fill_rect(x, y, w, @wm_sub_h, WM_BG)
    @gfx.draw_rect(x, y, w, @wm_sub_h, FmrbConst::THEME_BORDER)
    rows = wm_rows
    i = 0
    while i < rows.size
      r = rows[i]
      iy = y + 1 + i * WM_ITEM_H
      break if iy + WM_ITEM_H > y + @wm_sub_h
      grey = r[2] == true
      right = r[1].to_s
      rw = right.empty? ? 0 : FmrbI18n.text_width(right) + WM_PAD
      room = w - WM_PAD * 2 - 8 - rw
      text = FmrbI18n.truncate_to(r[0].to_s, room)
      # The mark of a checked name: the router binds every node ID to its
      # certificate (a tls/ router), so the name is not just claimed.
      if @wm_verified && !r[3].to_s.empty?
        @gfx.fill_circle(x + WM_PAD + 2, iy + 6, 2, grey ? WM_GREY : FmrbConst::THEME_HIGHLIGHT)
      end
      @gfx.draw_text(x + WM_PAD + 8, iy + 2, text, grey ? WM_GREY : WM_TEXT, WM_BG, mixed: true)
      @gfx.draw_text(x + w - rw, iy + 2, right, WM_GREY, WM_BG, mixed: true) unless right.empty?
      i += 1
    end
    nil
  end

  # ---- clicks and keys ----

  def hit_window_menu?(x, y)
    return false unless @wm_open
    return true if x >= @wm_x && x < @wm_x + WM_W && y >= @wm_y && y < @wm_y + wm_h
    @wm_sub && x >= @wm_sub_x && x < @wm_sub_x + @wm_sub_w &&
      y >= @wm_sub_y && y < @wm_sub_y + @wm_sub_h
  end

  def handle_window_menu_click(x, y)
    if @wm_sub && x >= @wm_sub_x && x < @wm_sub_x + @wm_sub_w &&
       y >= @wm_sub_y && y < @wm_sub_y + @wm_sub_h
      wm_sub_click((y - @wm_sub_y - 1) / WM_ITEM_H)
      return nil
    end
    return nil unless x >= @wm_x && x < @wm_x + WM_W && y >= @wm_y && y < @wm_y + wm_h
    idx = (y - @wm_y - 1) / WM_ITEM_H
    return nil if idx < 0 || idx >= @wm_items.size
    key = @wm_items[idx]
    pid = @wm_pid
    if key == :wm_send
      @wm_sub = !@wm_sub
      wm_layout_sub if @wm_sub
      wm_overlay_changed
    elsif key == :wm_reload
      close_window_menu
      # The kernel asks "Reload <app>?" as before (ask_reload).
      send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
                   { "cmd" => "window_reload", "target" => pid })
    else
      name = @wm_name
      close_window_menu
      # The app's quit request, by the path the long press's Yes takes
      # (the kernel checks the slot still holds that app).
      cb = {}
      cb["cmd"] = "close_confirmed"
      cb["close_pid"] = pid
      cb["close_name"] = name
      send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, cb)
    end
    nil
  end

  def wm_sub_click(idx)
    rows = wm_rows
    return nil if idx < 0 || idx >= rows.size
    what = rows[idx][3].to_s
    return nil if what.empty?
    if what.start_with?("?")
      key = :wm_peer_off
      key = :wm_peer_not_us if what == "?3"
      key = :wm_peer_no_apps if what == "?4"
      wm_note(rows[idx][0].to_s, FmrbI18n.t(key), WM_NOTE_FAIL_MS)
      return nil
    end
    pid = @wm_pid
    name = @wm_name
    label = rows[idx][0].to_s
    close_window_menu
    why = FmrbApp.move(pid, what).to_s
    if why.empty?
      @wm_moving_pid = pid
      @wm_moving_name = name
      Log.info("Desktop: window menu: moving PID #{pid} (#{name}) to #{what}")
      wm_note(wm_fill(:wm_sending, name), label, WM_MOVE_WAIT_MS)
    else
      Log.info("Desktop: window menu: PID #{pid} not moved to #{what}: #{why}")
      wm_note(wm_fill(:wm_not_sent, name), why, WM_NOTE_FAIL_MS)
    end
    nil
  end

  # Esc closes the menu; it swallows every other key while it is up.
  def handle_window_menu_key(scancode)
    close_window_menu if scancode == FmrbConst::KEY_ESC
    nil
  end

  # "checkpoint_result" for the move the menu asked for.
  def window_menu_moved(msg)
    pid = (msg["pid"] || -1).to_i
    return nil if pid != @wm_moving_pid || pid < 0
    @wm_moving_pid = -1
    to = msg["to"].to_s
    if msg["ok"] == true
      wm_note(wm_fill(:wm_sent, @wm_moving_name), to, WM_NOTE_MS)
    else
      wm_note(wm_fill(:wm_not_sent, @wm_moving_name), msg["reason"].to_s, WM_NOTE_FAIL_MS)
    end
    nil
  end

  # ---- the plate ----

  def wm_note(line1, line2, ms)
    @wm_note1 = line1
    @wm_note2 = line2
    @wm_note_until = Machine.board_millis + ms
    update_composite_regions
    draw_foreground
    nil
  end

  def wm_note_shown?
    @wm_note_until > 0
  end

  # From on_update: the plate goes when its time is up.
  def tick_window_menu
    return nil unless wm_note_shown?
    return nil if Machine.board_millis < @wm_note_until
    @wm_note_until = 0
    @wm_moving_pid = -1
    update_composite_regions
    draw_foreground
    nil
  end

  def wm_note_w
    w = FmrbI18n.text_width(@wm_note1)
    w2 = FmrbI18n.text_width(@wm_note2)
    w = w2 if w2 > w
    w += WM_PAD * 2
    w = @window_width - 8 if w > @window_width - 8
    w = 60 if w < 60
    w
  end

  def wm_note_x
    (@window_width - wm_note_w) / 2
  end

  def draw_window_menu_note
    return nil unless wm_note_shown?
    w = wm_note_w
    x = wm_note_x
    y = WM_NOTE_Y
    @gfx.fill_rect(x, y, w, WM_NOTE_H, WM_BG)
    @gfx.draw_rect(x, y, w, WM_NOTE_H, FmrbConst::THEME_BORDER)
    room = w - WM_PAD * 2
    @gfx.draw_text(x + WM_PAD, y + 4, FmrbI18n.truncate_to(@wm_note1, room), WM_TEXT, WM_BG, mixed: true)
    @gfx.draw_text(x + WM_PAD, y + 14, FmrbI18n.truncate_to(@wm_note2, room), WM_GREY, WM_BG, mixed: true)
    nil
  end

  # A string with "%s" replaced by name (as close_app_question does).
  def wm_fill(key, name)
    t = FmrbI18n.t(key)
    at = t.index("%s")
    return "#{t} #{name}" unless at
    t[0, at] + name + t[at + 2, t.length - at - 2]
  end
end
