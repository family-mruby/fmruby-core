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
    if info && info[:fullscreen]
      enter_fullscreen(new_pid)
    elsif @fullscreen_pid
      # A windowed app started from a fullscreen app -- the editor's F5 with a
      # windowed target. The desktop is suspended and mouse input is pinned to
      # the fullscreen app, so the new window came up invisible and unreachable
      # (clicks went to the fullscreen app underneath it). Park the fullscreen
      # app instead: the desktop comes back and the window behaves like any
      # other. cleanup_terminated_app brings the parked app back when the window
      # closes, which is what makes "F5, look, close, keep editing" work.
      if can_park_fullscreen?(@fullscreen_pid)
        Log.info("Windowed pid=#{new_pid} spawned from fullscreen pid=#{@fullscreen_pid}: parking")
        park_fullscreen
      end
    end

    # NOTE: keep the collection-class name (S-e-t) out of kernel source even in
    # comments -- Spinel splices `require "set"` on a bareword match and its
    # bundled library fails to compile in this program.
    _set_hid_target(new_pid)
    @hid_target_pid = new_pid
    Log.info("HID target set to new app pid=#{new_pid}")
    notify_apps_changed
    true
  end

  # Tell the desktop the set of running apps changed, so the taskbar rebuilds
  # now instead of when its once-a-second poll next notices. Unlike focus, which
  # moves from a dozen places and is reported from the tick, a process starting
  # or ending has exactly two funnels -- this one and cleanup_terminated_app --
  # so telling the desktop from them cannot miss a case either.
  def notify_apps_changed
    return unless @desktop_pid
    data = MessagePack.pack({ "cmd" => "apps_changed" })
    _send_raw_message(@desktop_pid, FmrbConst::MSG_TYPE_APP_CONTROL, data)
  end

  # A Run request names a file for the spawner to load, and any absolute path
  # will do: /app and /home are the usual homes, /tmp is where a scratch buffer
  # from the editor lands, an SD card is just another mount. There was a /app +
  # /home allowlist here, but the spawn command the shell and launcher use never
  # had one, so the same file ran from the shell and was refused by the editor's
  # F5. What this still rejects is a built-in name ("default/editor"): those are
  # not files, and the desktop is what spawns them.
  #
  # The other two callers read this as "is this pid a file-backed app?". A
  # built-in has no :path at all, so it answers false for them as it did before.
  def run_path_allowed?(path)
    return false if path.nil?
    path.start_with?("/")
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
  #
  # Fullscreen is a stack, not a flag. A fullscreen app can start another one --
  # the fullscreen editor runs a fullscreen game with F5 -- and when the inner
  # app ends the outer one has to come back to fullscreen. With a single
  # @fullscreen_pid slot and one flat suspend list, the inner app's
  # enter_fullscreen overwrote both, so its exit resumed *everything* including
  # the desktop: the taskbar reappeared over the editor, @fullscreen_pid was
  # left nil while a fullscreen app was still on screen (which also silently
  # disabled Ctrl+Q), and mouse routing no longer matched what was visible.
  #
  # Each frame records the apps IT suspended, so popping a frame resumes exactly
  # those: the inner frame resumes the outer fullscreen app and leaves the
  # desktop suspended.
  #
  #   @fs_stack       [{ pid:, suspended: [pid, ...] }, ...] -- innermost last
  #   @fullscreen_pid pid of the innermost frame, nil when the stack is empty
  #   @suspended_pids union over the frames (app_suspended? reads it)

  def is_fullscreen_app?(pid)
    win = find_window_by_pid(pid)
    win && win[:fullscreen] == true
  end

  def fs_top_pid
    n = @fs_stack.length
    n == 0 ? nil : @fs_stack[n - 1][:pid]
  end

  def fs_frame?(pid)
    i = 0
    while i < @fs_stack.length
      return true if @fs_stack[i][:pid] == pid
      i += 1
    end
    false
  end

  def enter_fullscreen(pid)
    Log.info("Entering fullscreen mode: PID #{pid} (depth #{@fs_stack.length + 1})")
    suspended = []

    # Suspend all other user apps (not kernel, not the fullscreen app itself).
    # Apps an outer frame already suspended are left to that frame, and a parked
    # app must not be picked up here -- popping this frame would resume it and
    # bring it back on screen behind the user's back.
    update_window_list
    @window_list.each do |win|
      next if win[:pid] == pid
      next if win[:app_name] == "system_desktop"  # Desktop suspends differently
      next if win[:pid] == 0  # kernel
      next if app_suspended?(win[:pid])
      next if win[:pid] == @parked_fullscreen_pid

      suspend_app(win[:pid])
      suspended << win[:pid]
      @suspended_pids << win[:pid]
    end

    # Notify desktop to stop drawing
    if @desktop_pid && !app_suspended?(@desktop_pid)
      suspend_app(@desktop_pid)
      suspended << @desktop_pid
      @suspended_pids << @desktop_pid
    end

    @fs_stack << { pid: pid, suspended: suspended }
    @fullscreen_pid = pid

    # Taking the screen means taking the top of the stacking order too.
    # Suspending the other apps does not hide their canvases, so an app that
    # was in front stayed in front: coming back from a park (Ctrl+Tab after
    # F5) drew the fullscreen editor *underneath* the window it had started.
    # Everything that takes the screen -- spawn, unpark, a nested frame --
    # comes through here, which is why the call belongs here and not at each
    # of those call sites.
    _bring_to_front(pid)
  end

  # Pop the frame belonging to +pid+ (and any frame above it, which can only be
  # stale) and resume exactly the apps those frames suspended. Returns the pid
  # of the fullscreen app that owns the screen afterwards, or nil.
  def pop_fullscreen_frames(pid)
    idx = nil
    i = 0
    while i < @fs_stack.length
      idx = i if @fs_stack[i][:pid] == pid
      i += 1
    end
    return fs_top_pid if idx.nil?

    while @fs_stack.length > idx
      frame = @fs_stack.pop
      frame[:suspended].each do |spid|
        resume_app(spid)
        drop_suspended(spid)
      end
    end
    @fullscreen_pid = fs_top_pid
    mark_window_list_dirty
    @fullscreen_pid
  end

  # Rebuild @suspended_pids without pid (Spinel mis-dispatches poly-receiver
  # Array#delete, same reason as in cleanup_terminated_app).
  def drop_suspended(pid)
    kept = []
    @suspended_pids.each { |sp| kept << sp unless sp == pid }
    @suspended_pids = kept
  end

  # ---- Runtime window <-> fullscreen switch ----
  #
  # Requested by a running app ({"cmd":"enter_fullscreen"} / "exit_fullscreen"),
  # so the editor can go fullscreen with F11 without being respawned -- the
  # buffer, cursor and scroll position stay because the VM never restarts.
  # The windowed geometry is remembered here so leaving fullscreen puts the
  # window back where it was.

  # Size a fullscreen window gets. The desktop's own window is created as
  # display size minus margin (fmrb_app_init, APP_TYPE_SYSTEM_APP), so it is the
  # display size the spawner would use -- no extra kernel API needed for it.
  def fullscreen_size
    update_window_list
    @window_list.each do |win|
      return [win[:width], win[:height]] if win[:app_name] == "system_desktop"
    end
    nil
  end

  def request_enter_fullscreen(pid)
    return if @fullscreen_pid == pid
    win = find_window_by_pid(pid)
    unless win
      update_window_list
      win = find_window_by_pid(pid)
    end
    if win
      @window_geometry = {} unless @window_geometry
      @window_geometry[pid] = { x: win[:x], y: win[:y], w: win[:width], h: win[:height] }
    end
    size = fullscreen_size
    unless size
      Log.warn("Runtime fullscreen refused: display size unknown")
      return
    end
    w = size[0]
    h = size[1]
    Log.info("Runtime fullscreen: PID #{pid} -> #{w}x#{h}")
    return unless _set_app_fullscreen(pid, true, w, h)
    enter_fullscreen(pid)
    _set_hid_target(pid)
    @hid_target_pid = pid
  end

  def request_exit_fullscreen(pid)
    return unless @fullscreen_pid == pid
    geom = @window_geometry ? @window_geometry[pid] : nil
    w = geom ? geom[:w] : 240
    h = geom ? geom[:h] : 200
    Log.info("Runtime windowed: PID #{pid} -> #{w}x#{h}")
    return unless _set_app_fullscreen(pid, false, w, h)
    _update_window_position(pid, geom ? geom[:x] : 5, geom ? geom[:y] : 15)
    # Give back whatever this app's frame had covered (desktop, or an outer
    # fullscreen app) and hand it the keyboard as a normal window.
    pop_fullscreen_frames(pid)
    _bring_to_front(pid)
    _set_hid_target(pid)
    @hid_target_pid = pid
  end

  # Ask the innermost fullscreen app to close. Restoring what was underneath is
  # left to cleanup_terminated_app, which runs on the app's exit notification --
  # doing it here as well used to declare "not fullscreen any more" while the app
  # was still on screen.
  def exit_fullscreen
    fs_pid = @fullscreen_pid
    return unless fs_pid
    Log.info("Exiting fullscreen mode: stopping PID #{fs_pid}")
    clear_data = MessagePack.pack({ "cmd" => "clear_and_stop" })
    _send_raw_message(fs_pid, FmrbConst::MSG_TYPE_APP_CONTROL, clear_data)
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
      if @parked_fullscreen_pid
        # Only one app parks at a time (single slot), and the parked one has to
        # stay reachable: cycle instead, which unparks it. The fullscreen stack
        # then holds both -- the app that was on screen is suspended underneath.
        Log.info("Ctrl+Tab: PID #{@parked_fullscreen_pid} is parked; cycling to it")
        cycle_front_app
        return
      end
      park_fullscreen
    else
      cycle_front_app
    end
  end

  # Can this fullscreen app be parked right now? Only one app can be parked at a
  # time: @parked_fullscreen_pid is a single slot, and a second park would leave
  # the first one suspended with no way back to it.
  def can_park_fullscreen?(pid)
    return false unless pid
    if @parked_fullscreen_pid
      Log.info("Park refused: PID #{@parked_fullscreen_pid} is already parked")
      return false
    end
    info = _get_app_info(pid)
    unless info && info[:fullscreen_switchable]
      Log.info("Park refused: PID #{pid} is not fullscreen_switchable")
      return false
    end
    true
  end

  # Suspend the fullscreen app (its on_suspend hides the canvas) and bring back
  # whatever its frame had suspended -- the desktop, or an outer fullscreen app
  # when this one was nested. The app keeps its state; unpark_fullscreen is the
  # way back.
  def park_fullscreen
    fs_pid = @fullscreen_pid
    return unless fs_pid
    Log.info("Parking fullscreen app PID #{fs_pid}")
    @parked_fullscreen_pid = fs_pid

    suspend_app(fs_pid)
    under = pop_fullscreen_frames(fs_pid)

    if under
      # A nested fullscreen app was underneath: it owns the screen now.
      _set_hid_target(under)
      @hid_target_pid = under
    elsif @desktop_pid
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

  # Tell the desktop who holds the keyboard, so the taskbar can mark it.
  #
  # Reported from the tick rather than from each place that moves focus: focus
  # moves from a spawn, Ctrl+Tab, a park or unpark, a click on a window, and an
  # app exiting -- a dozen call sites, and the marker was wrong whenever one of
  # them was missed. It used to be set only by a click on the taskbar itself, so
  # the white frame was absent most of the time. One comparison here cannot miss
  # a case, and a focus change is a user action, so this sends nothing while the
  # machine sits still.
  def tick_focus_notify
    return unless @desktop_pid
    return if @hid_target_pid == @notified_focus_pid
    data = MessagePack.pack({ "cmd" => "focus_changed", "pid" => @hid_target_pid })
    sent = _send_raw_message(@desktop_pid, FmrbConst::MSG_TYPE_APP_CONTROL, data)
    # Record only what actually went out: a full desktop queue would otherwise
    # lose this move for good, and the next tick is a cheap place to retry.
    @notified_focus_pid = @hid_target_pid if sent
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

    # A fullscreen app terminated: pop its frame, which resumes exactly what that
    # frame had suspended. Nested (editor -> fullscreen game) restores the outer
    # fullscreen app and leaves the desktop suspended.
    if fs_frame?(pid)
      Log.info("Fullscreen app terminated, restoring what it covered")
      restored = pop_fullscreen_frames(pid)
      if restored
        Log.info("Fullscreen restored to PID #{restored}")
        _set_hid_target(restored)
        @hid_target_pid = restored
      end
    end

    # A parked fullscreen app that terminated must not be unparked later
    @parked_fullscreen_pid = nil if @parked_fullscreen_pid == pid

    # Hand the keyboard back if this was the target app. An app started by Run
    # returns it to whoever asked for the Run (the editor, so F5 works again
    # without clicking); anything else falls back to the desktop. Leaving it
    # unassigned means the next key press goes nowhere until the user clicks.
    # cleanup_terminated_app can run twice for the same pid (exit notification
    # and then the reaper). Once an unpark is armed, the keyboard belongs to the
    # app that unpark will bring back -- a second pass must not hand it to the
    # desktop in between.
    if @hid_target_pid == pid && @pending_unpark.nil?
      back_to = @run_parent ? @run_parent[pid] : nil
      back_to = nil unless back_to && _get_app_info(back_to)
      if back_to && back_to == @parked_fullscreen_pid
        # The app that ran this one is a parked fullscreen app (the editor ran a
        # windowed app with F5): bring it back to fullscreen. Handing the
        # keyboard to a suspended app would look like a dead editor.
        #
        # Deferred on purpose: this app is still exiting and still in the window
        # list, so unparking here would have enter_fullscreen suspend a task in
        # the middle of its own teardown -- it then never finishes and the whole
        # desktop stalls. tick_process retries until the slot is actually free.
        @pending_unpark = back_to
        @pending_unpark_after = pid
        @pending_unpark_tries = 0
        Log.info("Unpark of PID #{back_to} deferred until pid=#{pid} is reaped")
      else
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

    # Sent after the reap, not before: the slot has to be free before the
    # desktop reads the process table, or the app it just lost is still listed.
    notify_apps_changed

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

      # Clean up HID target if it's gone. The window list only holds apps that
      # already reached RUNNING, so an app that was just handed the keyboard can
      # still be in INIT and absent from it -- checking the list alone used to
      # clear the target of a perfectly good app, and every hotkey (Ctrl+Q
      # included) then went nowhere. The context lookup knows about INIT.
      if @hid_target_pid && !window_pids.include?(@hid_target_pid) &&
         !_get_app_info(@hid_target_pid)
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

  # Deferred unpark (see cleanup_terminated_app). Waits for the windowed app's
  # slot to be free, so enter_fullscreen cannot suspend a task that is still
  # shutting down (that stalls it forever: the task never finishes exiting).
  UNPARK_MAX_TRIES = 100

  def flush_pending_unpark
    pid = @pending_unpark
    return unless pid
    @pending_unpark_tries += 1
    waiting = @pending_unpark_after
    if waiting && _get_app_info(waiting) && @pending_unpark_tries < UNPARK_MAX_TRIES
      return  # still terminating; try again next tick
    end
    @pending_unpark = nil
    @pending_unpark_after = nil
    return unless pid == @parked_fullscreen_pid
    unless _get_app_info(pid)
      Log.info("Deferred unpark dropped: PID #{pid} is gone")
      @parked_fullscreen_pid = nil
      return
    end
    unpark_fullscreen
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

    # Bring a parked fullscreen app back once the windowed app it ran is gone.
    flush_pending_unpark

    # Keep the taskbar's focus marker honest.
    tick_focus_notify

    # Periodic cleanup check for terminated apps
    if @tick_count - @last_cleanup_tick >= @cleanup_interval
      check_terminated_apps
      @last_cleanup_tick = @tick_count
    end
  end
end
