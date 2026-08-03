# App Lifecycle module for FmrbKernelImpl
# Fullscreen mode, suspend/resume, app termination cleanup, periodic tasks

module AppLifecycleMixin
  # ---- Spawn follow-up ----

  # Housekeeping every spawn needs, whoever asked for it: refresh the window
  # list, honour a fullscreen app, and hand the keyboard to the new app.
  #
  # It used to live in the "spawn" message handler only, so an app started any
  # other way came up without the keyboard and needed a click on its canvas
  # first -- the debug daemon spawns through the C API, and the reload path
  # skipped the fullscreen step as well.
  def after_spawn(new_pid)
    return false unless new_pid

    mark_window_list_dirty

    # Read fullscreen from the app context rather than the window list: the
    # list is refreshed asynchronously and is not there yet at this point.
    info = _get_app_info(new_pid)
    enter_fullscreen(new_pid) if info && info[:fullscreen]

    # NOTE: keep the collection-class name (S-e-t) out of kernel source even in
    # comments -- Spinel splices `require "set"` on a bareword match and its
    # bundled library fails to compile in this program.
    _set_hid_target(new_pid)
    @hid_target_pid = new_pid
    Log.info("HID target set to new app pid=#{new_pid}")
    true
  end

  # A Run request may only name a file the spawner can load: user apps live
  # under /app, user files under /home. Built-in names ("default/editor") are
  # deliberately not runnable this way -- the desktop spawns those.
  def run_path_allowed?(path)
    return false if path.nil?
    return false if path.empty?
    path.start_with?("/app/") || path.start_with?("/home/")
  end

  # Tell a Run requester (the editor) which pid its file is running as, and
  # remember the parentage so the keyboard goes back there when the app ends.
  def reply_run_result(requester, path, new_pid)
    return unless requester
    if new_pid
      @run_parent = {} unless @run_parent
      @run_parent[new_pid] = requester
    end
    reply = { "cmd" => "run_result", "path" => path, "pid" => new_pid }
    _send_raw_message(requester, FmrbConst::MSG_TYPE_APP_CONTROL, MessagePack.pack(reply))
  end

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

    # Stop any APU voices the app was holding. An app that ends normally does
    # this itself in on_destroy, but one that dies on an exception never gets
    # there and the note would sound forever.
    silence_notes_for(pid)

    # If fullscreen app terminated, resume all suspended apps
    if @fullscreen_pid == pid
      Log.info("Fullscreen app terminated, resuming suspended apps")
      @fullscreen_pid = nil
      @suspended_pids.each do |spid|
        resume_app(spid)
      end
      @suspended_pids = []
    end

    # Hand the keyboard back if this was the target app. An app started by Run
    # returns it to whoever asked for the Run (the editor, so F5 works again
    # without clicking); anything else falls back to the desktop. Leaving it
    # unassigned means the next key press goes nowhere until the user clicks.
    if @hid_target_pid == pid
      back_to = @run_parent ? @run_parent[pid] : nil
      back_to = nil unless back_to && _get_app_info(back_to)
      back_to = @desktop_pid if back_to.nil?
      if back_to
        _set_hid_target(back_to)
        @hid_target_pid = back_to
        Log.info("HID target back to pid=#{back_to} (app #{pid} terminated)")
      else
        _set_hid_target(0xFF)
        @hid_target_pid = nil
        Log.info("Cleared HID target (app #{pid} terminated)")
      end
    end
    @run_parent.delete(pid) if @run_parent

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

    # Remove all topic subscriptions for terminated app. Rebuild each list
    # without pid (Spinel mis-dispatches poly-receiver Array#delete to
    # String#delete; pids here is a poly hash value).
    empty_topics = []
    @subscriptions.each do |topic, pids|
      kept = []
      pids.each { |sp| kept << sp unless sp == pid }
      if kept.empty?
        empty_topics << topic
      else
        @subscriptions[topic] = kept
      end
    end
    empty_topics.each { |topic| @subscriptions.delete(topic) }

    # Handle pending reload (re-spawn after termination). Editor RUN uses the
    # same slot: stopping first means the old instance has released its memory
    # pool before the new one asks for it.
    if @pending_reload
      path = @pending_reload[:path]
      requester = @pending_reload[:requester]
      @pending_reload = nil
      Log.info("Reload: re-spawning #{path}")
      new_pid = _spawn_app_req(path)
      if new_pid
        after_spawn(new_pid)
        Log.info("Reload: spawned PID #{new_pid}")
      else
        Log.error("Reload: failed to spawn #{path}")
      end
      reply_run_result(requester, path, new_pid) if requester
    end

    # Reap the parked task: deletes its FreeRTOS task and frees the slot.
    # The app self-cleans its resources then parks; we delete it from this
    # kernel context to avoid SMP self-delete races (idempotent).
    _reap_app(pid)

    # Force immediate check (window list update after task deletion)
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
