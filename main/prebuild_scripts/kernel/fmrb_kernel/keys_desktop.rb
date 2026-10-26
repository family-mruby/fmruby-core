# Keyboard-only desktop support for FmrbKernelImpl (doc/linux_native, L4)
#
# The keys desktop (system_desktop_keys.app.rb) tells the kernel at start
# where apps go ("desktop_keys": the rectangle under its bar). From then on,
# and only then, every app with a window is shown full screen in that
# rectangle, one at a time:
#
#   - the app in front runs; the others are suspended (parked) and keep
#     their state;
#   - the desktop is never suspended: its bar stays on screen above the app,
#     and its menu, switcher and panels cover the app while they are up;
#   - the switcher key (the kernel's cycle_focus) asks the desktop for the
#     switcher instead of cycling windows; the desktop answers with
#     keys_show / keys_menu / keys_close.
#
# None of this touches the fullscreen stack (@fs_stack, @parked_fullscreen_pid):
# with the mouse desktop @keys_mode stays false and every path below returns
# at its first line, so the mouse desktop's fullscreen, Ctrl+Tab park and
# nesting behave exactly as before.
#
#   @keys_mode       true once the keys desktop has announced itself
#   @keys_x/y/w/h    the rectangle apps are placed in
#   @keys_front_pid  the app in front, -1 when the desktop's menu is
#   @keys_recent     the apps that have been in front, the most recent last
#                    (L5a): what comes back when the app in front ends
module KeysDesktopMixin
  def keys_init
    @keys_mode = false
    @keys_x = 0
    @keys_y = 0
    @keys_w = 0
    @keys_h = 0
    @keys_front_pid = -1
    @keys_recent = []
    nil
  end

  # "desktop_keys" from the desktop. Ignored from anyone else.
  def keys_enable(pid, data)
    return nil unless pid == @desktop_pid
    @keys_mode = true
    @keys_x = (data["x"] || 0).to_i
    @keys_y = (data["y"] || 0).to_i
    @keys_w = (data["w"] || 0).to_i
    @keys_h = (data["h"] || 0).to_i
    Log.info("Keys desktop: apps at (#{@keys_x},#{@keys_y}) #{@keys_w}x#{@keys_h}")
    nil
  end

  # An app has its canvas: place it in the app rectangle, full screen, and
  # bring it to the front. Headless apps (the service host) have no window.
  def keys_place(pid)
    info = _get_app_info(pid)
    return nil unless info
    return nil if info[:headless]
    Log.info("Keys desktop: PID #{pid} full screen at (#{@keys_x},#{@keys_y}) #{@keys_w}x#{@keys_h}")
    _set_app_fullscreen(pid, true, @keys_w, @keys_h)
    _update_window_position(pid, @keys_x, @keys_y)
    mark_window_list_dirty
    keys_show(pid)
    nil
  end

  # Bring +pid+ to the front: park the one there, wake this one, hand it the
  # keyboard, and tell the desktop.
  def keys_show(pid)
    return nil unless _get_app_info(pid)
    old = @keys_front_pid
    keys_park(old) if old >= 0 && old != pid
    if app_suspended?(pid)
      resume_app(pid)
      drop_suspended(pid)
    end
    @keys_front_pid = pid
    keys_recent_drop(pid)
    @keys_recent << pid
    _bring_to_front(pid)
    _set_hid_target(pid)
    @hid_target_pid = pid
    mark_window_list_dirty
    keys_notify_front
    nil
  end

  # The desktop's menu in front: park the app there and give the desktop the
  # keyboard.
  def keys_menu
    old = @keys_front_pid
    keys_park(old) if old >= 0
    @keys_front_pid = -1
    keys_focus_desktop
    keys_notify_front
    nil
  end

  def keys_park(pid)
    return nil if app_suspended?(pid)
    return nil unless _get_app_info(pid)
    suspend_app(pid)
    @suspended_pids << pid
    nil
  end

  def keys_focus_desktop
    return nil unless @desktop_pid
    _set_hid_target(@desktop_pid)
    @hid_target_pid = @desktop_pid
    nil
  end

  def keys_notify_front
    return nil unless @desktop_pid
    data = MessagePack.pack({ "cmd" => "keys_front", "pid" => @keys_front_pid })
    _send_raw_message(@desktop_pid, FmrbConst::MSG_TYPE_APP_CONTROL, data)
    nil
  end

  # The switcher key: the desktop draws the list over the app in front and
  # takes the keyboard until a choice is made.
  def keys_open_switcher
    return nil unless @desktop_pid
    keys_focus_desktop
    data = MessagePack.pack({ "cmd" => "keys_switcher" })
    _send_raw_message(@desktop_pid, FmrbConst::MSG_TYPE_APP_CONTROL, data)
    nil
  end

  # "Quit" from the switcher: the app comes to the front first (a built-in
  # may want to ask about unsaved work, and a parked app cannot read the
  # request), then it is asked to end, the way Ctrl+Q asks. One that was
  # behind goes in the order of use just under the app that was in front,
  # so that app comes back when it ends (keys_after_exit).
  def keys_close(pid)
    info = _get_app_info(pid)
    return nil unless info
    return nil if pid == @desktop_pid
    front = @keys_front_pid
    keys_show(pid)
    if front >= 0 && front != pid
      keys_recent_drop(front)
      @keys_recent << front
    end
    _mark_expected_stop(pid)
    cmd = run_path_allowed?(info[:path].to_s) ? "stop" : "quit_request"
    Log.info("Keys desktop: #{cmd} to PID #{pid}")
    _send_raw_message(pid, FmrbConst::MSG_TYPE_APP_CONTROL, MessagePack.pack({ "cmd" => cmd }))
    nil
  end

  # From cleanup_terminated_app, after the keyboard has been handed back. The
  # app that was in front is gone: the app the keyboard went to (the editor
  # an F5 run came from) comes to the front, else the app used before it
  # (@keys_recent), else the menu. An app that ended behind the one in front
  # changes nothing on screen.
  def keys_after_exit(pid)
    return nil unless @keys_mode
    # A parked app that ended is not suspended any more.
    drop_suspended(pid) if app_suspended?(pid)
    keys_recent_drop(pid)
    return nil unless pid == @keys_front_pid
    @keys_front_pid = -1
    to = -1
    back = @hid_target_pid
    if back && back != @desktop_pid && back != pid && _get_app_info(back)
      to = back
    else
      to = keys_recent_last(pid)
    end
    if to >= 0
      keys_show(to)
    else
      keys_focus_desktop
      keys_notify_front
    end
    nil
  end

  def keys_recent_drop(pid)
    kept = []
    @keys_recent.each { |p| kept << p unless p == pid }
    @keys_recent = kept
    nil
  end

  # The most recently used app that is still there (not +gone+, not the
  # desktop), or -1.
  def keys_recent_last(gone)
    i = @keys_recent.size - 1
    while i >= 0
      p = @keys_recent[i]
      i -= 1
      next if p == gone || p == @desktop_pid
      return p if _get_app_info(p)
    end
    -1
  end

  # Control messages only the keys desktop sends. True when handled.
  def keys_control(cmd, pid, data)
    case cmd
    when "desktop_keys"
      keys_enable(pid, data)
      return true
    when "keys_show"
      return true unless @keys_mode
      target = (data["pid"] || -1).to_i
      if target >= 0
        keys_show(target)
      else
        keys_menu
      end
      return true
    when "keys_menu"
      keys_menu if @keys_mode
      return true
    when "keys_close"
      keys_close((data["pid"] || -1).to_i) if @keys_mode
      return true
    end
    false
  end
end
