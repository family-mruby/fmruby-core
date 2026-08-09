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

  # ---- Ctrl+Tab focus cycling / fullscreen park & unpark ----

  # Ctrl+Tab. In fullscreen: park the app (if it declared itself switchable)
  # and give the desktop back. Otherwise: cycle the focused front app through
  # the open windows; the keyboard follows the window. Reaching the parked
  # fullscreen app in the cycle brings it back to fullscreen.
  def handle_cycle_focus
    if @fullscreen_pid
      info = _get_app_info(@fullscreen_pid)
      unless info && info[:fullscreen_switchable]
        Log.info("Ctrl+Tab ignored: fullscreen app is not switchable")
        return
      end
      park_fullscreen
    else
      cycle_front_app
    end
  end

  # Suspend the fullscreen app (its on_suspend hides the canvas), bring the
  # desktop and the previously suspended apps back. The app keeps its state;
  # unpark_fullscreen is the way back.
  def park_fullscreen
    fs_pid = @fullscreen_pid
    Log.info("Parking fullscreen app PID #{fs_pid}")
    @fullscreen_pid = nil
    @parked_fullscreen_pid = fs_pid

    suspend_app(fs_pid)
    @suspended_pids.each { |spid| resume_app(spid) }
    @suspended_pids = []

    if @desktop_pid
      _set_hid_target(@desktop_pid)
      @hid_target_pid = @desktop_pid
    end
    mark_window_list_dirty
  end

  # Re-enter fullscreen for the parked app: suspend everything else again,
  # then resume the app (its on_resume redraws the screen).
  def unpark_fullscreen
    pid = @parked_fullscreen_pid
    return unless pid
    Log.info("Unparking fullscreen app PID #{pid}")
    @parked_fullscreen_pid = nil

    enter_fullscreen(pid)
    resume_app(pid)
    _set_hid_target(pid)
    @hid_target_pid = pid
    mark_window_list_dirty
  end

  # Round-robin focus over open windows (desktop excluded), keyboard included,
  # so the editor and a running app can be toggled without the mouse. The
  # parked fullscreen app takes its turn in the cycle via unpark.
  def cycle_front_app
    update_window_list
    candidates = []
    @window_list.each do |win|
      next if win[:pid] == 0
      next if win[:app_name] == "system_desktop"
      candidates << win[:pid]
    end
    candidates.sort!
    return if candidates.empty?

    idx = candidates.index(@hid_target_pid)
    # From the desktop (focus not on any window), returning to the parked
    # presentation is the most likely intent -- one press, not a lap around
    # the window ring.
    next_pid = if idx
                 candidates[(idx + 1) % candidates.size]
               elsif @parked_fullscreen_pid && candidates.include?(@parked_fullscreen_pid)
                 @parked_fullscreen_pid
               else
                 candidates[0]
               end
    return if next_pid == @hid_target_pid && candidates.size == 1 &&
              next_pid != @parked_fullscreen_pid

    if next_pid == @parked_fullscreen_pid
      unpark_fullscreen
    else
      _bring_to_front(next_pid)
      _set_hid_target(next_pid)
      @hid_target_pid = next_pid
      mark_window_list_dirty
      Log.info("Ctrl+Tab: focus -> PID #{next_pid}")
    end
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

    # A parked fullscreen app that terminated must not be unparked later
    @parked_fullscreen_pid = nil if @parked_fullscreen_pid == pid

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

  # Retry the deferred "open this file" message for a just-spawned app. The
  # app's queue is registered by its own task, so the send right after spawn
  # is dropped; a handful of ticks later it lands. Give up after
  # OPEN_PATH_MAX_TRIES so a crashed app cannot leave this armed forever.
  OPEN_PATH_MAX_TRIES = 60

  def flush_open_path
    return unless @open_path_pid
    @open_path_tries += 1
    opened = { "cmd" => "file_selected", "path" => @open_path, "mode" => "open" }
    sent = _try_send_raw_message(@open_path_pid, FmrbConst::MSG_TYPE_APP_CONTROL,
                                 MessagePack.pack(opened))
    if sent
      Log.info("Spawn open_path #{@open_path} -> pid=#{@open_path_pid}")
    elsif @open_path_tries < OPEN_PATH_MAX_TRIES
      return
    else
      Log.warn("Spawn open_path #{@open_path} not delivered to pid=#{@open_path_pid}")
    end
    @open_path_pid = nil
    @open_path = nil
    @open_path_tries = 0
  end

  # ---- Periodic tasks ----

  def tick_process
    # Periodic kernel tasks
    @tick_count += 1

    # A mouse move latched against a full app queue (input_router) is
    # delivered here once the app drains, so the cursor's final position is
    # never lost even when every move during the jam was dropped.
    flush_pending_move

    # Deliver a pending spawn open_path once the new app has its queue.
    flush_open_path

    # Periodic cleanup check for terminated apps
    if @tick_count - @last_cleanup_tick >= @cleanup_interval
      check_terminated_apps
      @last_cleanup_tick = @tick_count
    end
  end
end
