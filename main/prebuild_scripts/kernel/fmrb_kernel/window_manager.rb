# Window Manager module for FmrbKernelImpl
# Window list management, hit testing, z-order tracking

$menu_bar_height = 13

module WindowManagerMixin

  def update_window_list(show_log = false)
    # Only update if dirty flag is set
    if @window_list_dirty
      @window_list = _get_window_list
      @window_list_dirty = false
    end

    if show_log
      Log.info("Window list (#{@window_list.size}):")
      @window_list.each do |w|
        Log.info("  PID #{w[:pid]} '#{w[:app_name]}' pos=(#{w[:x]},#{w[:y]}) size=#{w[:width]}x#{w[:height]} Z=#{w[:z_order]}")
      end
    end
  end

  def mark_window_list_dirty
    @window_list_dirty = true
  end

  def find_window_by_pid(pid)
    # Find window by PID
    @window_list.each do |win|
      return win if win[:pid] == pid
    end
    nil
  end

  def find_window_at(x, y)
    # Desktop foreground (z=254) special hit-testing:
    # 1. Menu bar region: always hit-testable
    # 2. Dropdown open: dropdown rect is hit-testable, outside closes it
    # 3. Otherwise: transparent (skip to windows below)
    if @desktop_pid
      if y < $menu_bar_height
        # Menu bar click -> route to desktop
        return find_window_by_pid(@desktop_pid)
      end

      if @desktop_overlay_active
        r = @desktop_overlay_rect
        if x >= r[:x] && x < r[:x] + r[:w] &&
           y >= r[:y] && y < r[:y] + r[:h]
          # Click inside dropdown -> route to desktop
          return find_window_by_pid(@desktop_pid)
        else
          # Click outside dropdown -> close it, then fall through
          _send_raw_message(@desktop_pid, FmrbConst::MSG_TYPE_HID_EVENT,
                            build_hid_close_overlay)
          @desktop_overlay_active = false
        end
      end
    end

    # Search from front to back (highest z_order first)
    target_window = nil
    max_z_order = -1

    @window_list.each do |win|
      # Skip desktop (handled above with special logic)
      next if win[:app_name] == "system_desktop"
      # Skip suspended apps
      next if app_suspended?(win[:pid])

      if x >= win[:x] && x <= win[:x] + win[:width] - 1 &&
         y >= win[:y] && y <= win[:y] + win[:height] - 1
        if win[:z_order] > max_z_order
          max_z_order = win[:z_order]
          target_window = win
        end
      end
    end

    target_window
  end

  def build_hid_close_overlay
    # Send a special mouse_up event at (0,0) to trigger dropdown close
    data = "\x00\x00\x00\x00\x00\x00"
    data.setbyte(0, 5)  # subtype: mouse_up
    data.setbyte(1, 1)  # button: left
    data
  end
end
