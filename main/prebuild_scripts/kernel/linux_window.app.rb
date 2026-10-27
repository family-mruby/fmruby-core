# The stand-in for one Linux window (doc/linux_native, L9a).
#
# The native program as a Wayland compositor (native/wlr/) starts one of these
# for every Linux window (native/wlr/wl_bridge.c). It is an ordinary windowed
# app: the window manager moves, resizes, raises and focuses it like any other
# window, and the compositor puts the Linux window where its content is.
#
# It draws only the Family mruby frame -- title bar (with the Linux window's
# title), border, close button, resize grip -- and fills the content with
# RGB332 0x01, the colour nothing else shows (it is the transparent key of
# every colour-keyed canvas). The compositor draws the Linux window under the
# picture and lets it through wherever those pixels are still on screen, so a
# Family mruby window or menu over the stand-in hides that part of the Linux
# window. Its canvas is not colour-keyed (rounded_corners = false at spawn),
# so the 0x01 pixels reach the screen.
#
# The close button asks the Linux window to close (it may ask about unsaved
# work, or refuse); the stand-in ends when the window has gone.
#
# Native program only: no device, simulation or browser build carries it
# (main/CMakeLists.txt leaves it out; FmrbApp.wl_window exists only there).

class LinuxWindowApp < FmrbApp
  HOLE = 0x01
  GRIP = 9          # the resize grip, inside the content's bottom-right corner
  POLL_MS = 100
  WAIT_POLLS = 50   # 5 s for the bridge to know this stand-in

  def on_create
    @title = name
    @alive = true
    @unknown = 0
    draw
  end

  def on_update
    st = FmrbApp.wl_window
    if st.nil?
      @unknown += 1
      if @unknown > WAIT_POLLS
        Log.warn("linux_window: no Linux window for this stand-in; ending")
        @alive = false
        stop
      end
    elsif st == false
      @alive = false
      stop
    elsif st != @title
      @title = st
      draw
    end
    POLL_MS
  end

  def on_resize(new_width, new_height)
    draw
  end

  def on_resume
    draw
  end

  # Every way this app is asked to end -- the close button, Ctrl+Q, the
  # kernel's stop -- asks the Linux window to close instead. The window
  # decides; when it has gone, on_update ends the stand-in.
  def stop
    if @alive && FmrbApp.wl_close
      redraw_close_button
      return nil
    end
    super
  end

  private

  def draw
    w = @window_width
    h = @window_height
    bar = FmrbConst::THEME_MENU_BG
    on_bar = FmrbConst::THEME_TEXT_LIGHT
    border = FmrbConst::THEME_BORDER
    @gfx.set_font(:default)
    @gfx.set_text_size(1)
    # The corners outside the rounded border: the canvas is opaque, so they
    # take the desktop's colour rather than the hole's.
    @gfx.fill_rect(0, 0, w, h, FmrbConst::THEME_DESKTOP_BG)
    @gfx.fill_round_rect(0, 0, w, TITLE_BAR_H, CORNER_R, bar)
    @gfx.fill_rect(0, CORNER_R, w, TITLE_BAR_H - CORNER_R, bar)
    @gfx.fill_rect(0, TITLE_BAR_H, w, h - TITLE_BAR_H - CORNER_R, border)
    @gfx.fill_round_rect(0, h - 2 * CORNER_R, w, 2 * CORNER_R, CORNER_R, border)
    # The content: the hole.
    @gfx.fill_rect(@user_area_x0, @user_area_y0, @user_area_width, @user_area_height, HOLE)
    # Menu button and title, as every window has them.
    @gfx.fill_rect(3, 3, 9, 1, on_bar)
    @gfx.fill_rect(3, 5, 9, 1, on_bar)
    @gfx.fill_rect(3, 7, 9, 1, on_bar)
    room = w - 15 - 14
    t = @title.to_s
    t = FmrbI18n.truncate_to(t, room) if @gfx.text_width(t) > room
    @gfx.draw_text(15, 2, t, on_bar)
    @gfx.fill_circle(w - CLOSE_BTN_CX_OFFSET, CLOSE_BTN_CY, CLOSE_BTN_R, on_bar)
    @gfx.draw_round_rect(0, 0, w, h, CORNER_R, border)
    # The resize grip: the window manager's corner (the bottom-right 10x10)
    # stays Family mruby's, so it is drawn rather than left a hole.
    gx = w - 1 - GRIP
    gy = h - 1 - GRIP
    @gfx.fill_rect(gx, gy, GRIP, GRIP, bar)
    @gfx.draw_line(gx + 2, gy + GRIP - 2, gx + GRIP - 2, gy + 2, on_bar)
    @gfx.draw_line(gx + 5, gy + GRIP - 2, gx + GRIP - 2, gy + 5, on_bar)
    @gfx.present
  end

  def redraw_close_button
    return unless @gfx
    @gfx.fill_circle(@window_width - CLOSE_BTN_CX_OFFSET, CLOSE_BTN_CY, CLOSE_BTN_R,
                     FmrbConst::THEME_TEXT_LIGHT)
    @gfx.present
  end
end

app = LinuxWindowApp.new
app.start
