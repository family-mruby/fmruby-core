# App Lifecycle module for FmrbKernelImpl
# Fullscreen mode, suspend/resume, app termination cleanup, periodic tasks

module AppLifecycleMixin
  # ---- Fullscreen mode management ----

  def is_fullscreen_app?(pid)
    win = find_window_by_pid(pid)
    win && win[:fullscreen] == true
  end

  def enter_fullscreen(pid)
    Log.info("Entering fullscreen mode: PID #{pid}")
    @fullscreen_pid = pid
    @suspended_pids = []

    # Suspend all other user apps (not kernel, not the fullscreen app itself)
    update_window_list
    @window_list.each do |win|
      next if win[:pid] == pid
      next if win[:app_name] == "system_desktop"  # Desktop suspends differently
      next if win[:pid] == 0  # kernel

      suspend_app(win[:pid])
      @suspended_pids << win[:pid]
    end

    # Notify desktop to stop drawing
    if @desktop_pid
      suspend_app(@desktop_pid)
      @suspended_pids << @desktop_pid
    end
  end

  def exit_fullscreen
    return unless @fullscreen_pid
    Log.info("Exiting fullscreen mode: stopping PID #{@fullscreen_pid}")

    # Stop the fullscreen app
    fs_pid = @fullscreen_pid
    @fullscreen_pid = nil

    # Send clear + stop to fullscreen app (clear canvas before stopping)
    clear_data = MessagePack.pack({ "cmd" => "clear_and_stop" })
    _send_raw_message(fs_pid, FmrbConst::MSG_TYPE_APP_CONTROL, clear_data)

    # Resume all suspended apps
    @suspended_pids.each do |spid|
      resume_app(spid)
    end
    @suspended_pids = []

    # Restore HID target to desktop
    if @desktop_pid
      _set_hid_target(@desktop_pid)
      @hid_target_pid = @desktop_pid
    end

    mark_window_list_dirty
  end

  def app_suspended?(pid)
    @suspended_pids && @suspended_pids.include?(pid)
  end

  def suspend_app(pid)
    _suspend_app(pid)
    Log.info("Suspended app PID #{pid}")
  end

  def resume_app(pid)
    _resume_app(pid)
    Log.info("Resumed app PID #{pid}")
  end

  # ---- App termination cleanup ----

  def cleanup_terminated_app(pid)
    Log.info("Cleaning up terminated app: pid=#{pid}")

    # If fullscreen app terminated, resume all suspended apps
    if @fullscreen_pid == pid
      Log.info("Fullscreen app terminated, resuming suspended apps")
      @fullscreen_pid = nil
      @suspended_pids.each do |spid|
        resume_app(spid)
      end
      @suspended_pids = []
    end

    # Reset HID target if this was the target app
    if @hid_target_pid == pid
      _set_hid_target(0xFF)
      @hid_target_pid = nil
      Log.info("Cleared HID target (app #{pid} terminated)")
    end

    # Release mouse capture if this app had it
    if @capture_pid == pid
      @capture_pid = nil
      @capture_mode = nil
      Log.info("Released mouse capture (app #{pid} terminated)")
    end

    # Clear mouse_down state if needed
    if @mouse_down_pid == pid
      @mouse_down_pid = nil
    end

    # Remove all topic subscriptions for terminated app
    empty_topics = []
    @subscriptions.each do |topic, pids|
      pids.delete(pid)
      empty_topics << topic if pids.empty?
    end
    empty_topics.each { |topic| @subscriptions.delete(topic) }

    # Handle pending reload (re-spawn after termination)
    if @pending_reload
      path = @pending_reload[:path]
      @pending_reload = nil
      Log.info("Reload: re-spawning #{path}")
      new_pid = _spawn_app_req(path)
      if new_pid
        mark_window_list_dirty
        _set_hid_target(new_pid)
        @hid_target_pid = new_pid
        Log.info("Reload: spawned PID #{new_pid}")
      else
        Log.error("Reload: failed to spawn #{path}")
      end
    end

    # Force immediate check (app may still be transitioning to STOPPING state)
    # This will be rechecked periodically until the app fully terminates
    mark_window_list_dirty
    check_terminated_apps
  end

  def check_terminated_apps
    # Get current window list and check for changes
    old_count = @window_list.size
    update_window_list
    new_count = @window_list.size

    if old_count != new_count
      Log.debug("Periodic cleanup: window list changed (#{old_count} -> #{new_count})")

      # Check if any tracked PIDs are no longer in the window list
      window_pids = @window_list.map { |w| w[:pid] }

      # Clean up HID target if it's gone
      if @hid_target_pid && !window_pids.include?(@hid_target_pid)
        Log.info("HID target app #{@hid_target_pid} no longer exists")
        @hid_target_pid = nil
        _set_hid_target(0xFF)
      end

      # Clean up capture if it's gone
      if @capture_pid && !window_pids.include?(@capture_pid)
        Log.info("Captured app #{@capture_pid} no longer exists")
        @capture_pid = nil
        @capture_mode = nil
      end

      # Clean up mouse_down if it's gone
      if @mouse_down_pid && !window_pids.include?(@mouse_down_pid)
        @mouse_down_pid = nil
      end
    end
  end

  # ---- Periodic tasks ----

  def tick_process
    # Periodic kernel tasks
    @tick_count += 1

    # Periodic cleanup check for terminated apps
    if @tick_count - @last_cleanup_tick >= @cleanup_interval
      check_terminated_apps
      @last_cleanup_tick = @tick_count
    end
  end
end
