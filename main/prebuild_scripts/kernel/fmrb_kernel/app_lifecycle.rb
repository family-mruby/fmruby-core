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
  # Whether an app may hold the keyboard.
  #
  # It has to have something on screen to type into. A headless app
  # (default_window_mode = "background" -- the service host is one) has no
  # window and no canvas, so giving it the keyboard takes input away from
  # whatever the user was using and hands it to something that cannot show a
  # cursor, a prompt or an error.
  #
  # One predicate, and every path that moves focus asks it: the hand-over
  # after a spawn, a click on a window, a click on a taskbar icon, and
  # Ctrl+Tab. They used to each decide for themselves, which is how a
  # background app came up owning the keyboard.
  def focusable?(pid)
    return false unless pid
    info = _get_app_info(pid.to_i)
    return false unless info
    return false if info[:headless]
    true
  end

  def after_spawn(new_pid)
    return false unless new_pid

    mark_window_list_dirty

    # Read fullscreen from the app context rather than the window list: the
    # list is refreshed asynchronously and is not there yet at this point.
    info = _get_app_info(new_pid)
    # A fullscreen app is not given the screen here. Spawn returns as soon as
    # the task exists and the script is loaded and compiled inside it, which
    # on the device takes seconds; entering fullscreen now would suspend the
    # desktop and hide its canvas (fmrb_app_suspend does both), leaving the
    # screen blank for the whole load with nothing able to report progress.
    # on_app_started hands the screen over when there is something to show.
    fullscreen_app = info && info[:fullscreen]
    if !fullscreen_app && @fullscreen_pid
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

    announce_starting(new_pid, info)

    # Nothing on screen to type into: report the new process and stop short of
    # the hand-over below (focusable?).
    can_focus = focusable?(new_pid)
    unless can_focus
      notify_apps_changed
      return true
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

  # ---- Start indicator ----
  #
  # From the spawn request until the app has its main canvas, the desktop shows
  # "<name> starting". Compiling a script takes seconds on the device and the
  # screen said nothing about it.
  #
  # Started is reported by fmrb_app_notify_started, which the shared canvas
  # helper calls -- one place every runtime (mruby, Spinel, MicroPython, Lua,
  # BASIC) reaches only after its script is loaded. A headless app never gets
  # there, which is why it is not announced at all.
  #
  # Taking it down again does not rely on that one message: a compile error
  # (app_error), the app dying (cleanup_terminated_app) and a timeout on the
  # desktop side each clear it.
  STARTING_TIMEOUT_MS = 20000

  def announce_starting(pid, info)
    return if info.nil? || info[:headless]
    return unless @desktop_pid
    name = info[:name] || ""
    @starting_pid = pid
    @starting_at = Machine.board_millis
    data = MessagePack.pack({ "cmd" => "app_starting", "pid" => pid, "name" => name })
    _send_raw_message(@desktop_pid, FmrbConst::MSG_TYPE_APP_CONTROL, data)
  end

  def clear_starting(pid)
    return unless @starting_pid
    return unless pid.nil? || pid == @starting_pid
    @starting_pid = nil
    @starting_at = nil
    return unless @desktop_pid
    data = MessagePack.pack({ "cmd" => "app_started" })
    _send_raw_message(@desktop_pid, FmrbConst::MSG_TYPE_APP_CONTROL, data)
  end

  # The app has its canvas: it is up. Hand it the screen if it asked for
  # fullscreen, and take the indicator down.
  def on_app_started(pid)
    return unless pid
    Log.info("App started: pid=#{pid}")
    # Clear before handing over the screen, not after: enter_fullscreen
    # suspends the desktop, and a message sent to a suspended app waits in its
    # queue. Done the other way round, an app that starts quickly (a Spinel
    # app is native code and has nothing to compile) left both the raise and
    # the clear queued, and the desktop played them back when something else
    # resumed it -- the indicator appeared minutes later, over an app that had
    # long since started.
    clear_starting(pid)
    # Whether it wants the screen is written on the app itself, so ask the
    # context rather than remembering. A remembered pid was a single slot:
    # two fullscreen apps starting close together (F5 from the editor while
    # one is still loading) overwrote it, and the first one never got the
    # screen.
    if take_spawn_fullscreen(pid)
      # The requester asked for this app to run full screen, overriding its
      # own window mode (kernel "spawn" with "fullscreen"). It has a canvas
      # now, so this is the runtime switch -- flag, geometry, screen and
      # keyboard in one -- and not the flag alone, which is refused while the
      # task is still starting.
      Log.info("Spawn fullscreen: PID #{pid}")
      request_enter_fullscreen(pid)
      return nil
    end
    info = _get_app_info(pid)
    enter_fullscreen(pid) if info && info[:fullscreen] && @fullscreen_pid != pid
    nil
  end

  # True once, for an app whose spawn asked for fullscreen. Taking the pid out
  # here is also what keeps a pid that never started (a compile error) from
  # applying to whatever app is given that slot next.
  def take_spawn_fullscreen(pid)
    list = @spawn_fullscreen_pids
    idx = list.index(pid)
    return false unless idx
    kept = []
    i = 0
    while i < list.size
      kept << list[i] unless i == idx
      i += 1
    end
    @spawn_fullscreen_pids = kept
    true
  end

  # Backstop for a start that never reports: a runtime that draws nothing but
  # keeps running would otherwise leave the indicator up for ever. Called from
  # the periodic tick.
  def tick_starting
    return unless @starting_at
    return if Machine.board_millis - @starting_at < STARTING_TIMEOUT_MS
    Log.warn("Start indicator timed out for pid=#{@starting_pid}")
    pid = @starting_pid
    info = pid ? _get_app_info(pid) : nil
    enter_fullscreen(pid) if info && info[:fullscreen] && @fullscreen_pid != pid
    clear_starting(nil)
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

  # ---- Kill on request (shell "kill <pid>", monitor task page) ----
  #
  # Cooperative only: the target is *asked* to end itself, the same way Ctrl+Q
  # asks. Nothing here deletes a task, so an app wedged in its own loop stays
  # -- forcing it is a separate job that must not run on the kernel task, which
  # would have to block for a second or more (doc/app_kill_fix).
  #
  # The policy lives here because every requester comes through this message
  # and only the kernel knows who asked: a target must be a user app, and never
  # the requester itself.
  def handle_kill_request(requester, target)
    pid = (target || -1).to_i

    if pid < FmrbConst::PROC_ID_USER_APP0
      # The kernel, the host, the desktop and the overlays sit below that id.
      reply_kill_result(requester, pid, false, "not a user task")
      return
    end
    if pid == requester
      reply_kill_result(requester, pid, false, "cannot end itself")
      return
    end

    info = _get_app_info(pid)
    if info.nil?
      reply_kill_result(requester, pid, false, "no such task")
      return
    end
    if app_suspended?(pid)
      # A suspended app never reads its queue, so the request would sit there
      # for good. Say so instead of leaving the requester waiting; with no
      # forced path in this stage there is nothing to fall back on.
      reply_kill_result(requester, pid, false, "suspended")
      return
    end

    name = info[:name].to_s
    path = info[:path].to_s
    # Asked for, so whatever comes of it is not a crash. Marked before the ask
    # rather than after: the app may be gone by the time the reply is written,
    # and the mark has to be on the context the reaper will read.
    _mark_expected_stop(pid)
    if run_path_allowed?(path)
      Log.info("Kill: stopping pid=#{pid} (#{path}) for pid=#{requester}")
      data = MessagePack.pack({ "cmd" => "stop" })
    else
      # A built-in gets the Ctrl+Q courtesy: it may hold unsaved work and
      # decides for itself (FmrbApp#on_quit_request defaults to stopping).
      Log.info("Kill: quit request to built-in pid=#{pid} for pid=#{requester}")
      data = MessagePack.pack({ "cmd" => "quit_request" })
    end
    sent = _send_raw_message(pid, FmrbConst::MSG_TYPE_APP_CONTROL, data)
    if sent
      reply_kill_result(requester, pid, true, name)
    else
      reply_kill_result(requester, pid, false, "queue full")
    end
  end

  # Answer a kill request. "ok" means the ask went out, not that the app is
  # gone: it ends itself when it next reads its queue, and the requester sees
  # that in the process list.
  def reply_kill_result(requester, pid, ok, info)
    return unless requester
    reply = { "cmd" => "kill_result", "pid" => pid, "ok" => ok, "info" => info }
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
    i = 0
    while i < @window_list.size
      win = @window_list[i]
      i += 1
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
    i = 0
    while i < @window_list.size
      win = @window_list[i]
      return [win[:width], win[:height]] if win[:app_name] == "system_desktop"
      i += 1
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
    # while, not each: Ctrl+Tab is an input path.
    i = 0
    while i < @window_list.size
      win = @window_list[i]
      i += 1
      next if win[:pid] == 0
      next if win[:app_name] == "system_desktop"
      # A window in the list belongs to an app with a canvas, but the ring is
      # a focus path and asks the same question the others do.
      win_focusable = focusable?(win[:pid])
      next unless win_focusable
      candidates << win[:pid]
    end
    candidates.sort!

    # The desktop is a stop on the ring, not something outside it. Its menu bar
    # and letter shortcuts only answer while it holds the keyboard, so with it
    # left out there was no way back to them once an app was running: Ctrl+Tab
    # went round the apps for ever and the menu could only be reached with the
    # mouse. It sorts first (pid 2 against 4 and up), so the ring reads
    # desktop, then the apps in the order they were started.
    #
    # Not while a fullscreen app is up, though: the desktop is suspended then
    # and its menu bar is not on screen, so handing it the keyboard would send
    # every key into a task that is not running.
    desktop_selectable = @desktop_pid && !@fullscreen_pid && !app_suspended?(@desktop_pid)
    candidates.unshift(@desktop_pid) if desktop_selectable
    return if candidates.empty?

    idx = candidates.index(@hid_target_pid)
    # From the desktop, returning to the parked presentation is the most likely
    # intent -- one press, not a lap around the window ring.
    parked_ready = @parked_fullscreen_pid && candidates.index(@parked_fullscreen_pid)
    next_pid = if @hid_target_pid == @desktop_pid && parked_ready
                 @parked_fullscreen_pid
               elsif idx
                 candidates[(idx + 1) % candidates.size]
               elsif parked_ready
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

  # Array#index, not include?: include? is Enumerable's Ruby one (a block call
  # per element, ~1.7 ms), and this is asked for every window on every mouse
  # event from find_window_at.
  def app_suspended?(pid)
    return false unless @suspended_pids
    @suspended_pids.index(pid) ? true : false
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

  # The service host is the one app the kernel restarts itself. Everything else
  # that wants to come back is the host's business (an "app =" entry with
  # restart = true), but nothing can bring the host back except the kernel: it
  # is where a user's resident code lives, and an app may not spawn another.
  #
  # Long enough that a host crashing on something at start-up cannot spin the
  # machine, short enough that a service is back before a person looks.
  SERVICE_RESPAWN_DELAY_MS = 2000
  # Three deaths inside five minutes means it is not going to work this boot.
  SERVICE_CRASH_WINDOW_MS = 300000
  SERVICE_CRASH_LIMIT = 3

  # System topic: an app is gone (doc/user_extension/services/plan.md).
  DIED_TOPIC = "app/died"

  # +expected+ comes from the app's own exit notification (whether it was
  # asked to end), not from its context: a kill can free the slot before this
  # runs, and then there is nothing left to ask. The periodic sweep, which has
  # no notification to read, passes true -- an app that vanished without one is
  # not something to restart on a guess.
  def cleanup_terminated_app(pid, expected = true)
    Log.info("Cleaning up terminated app: pid=#{pid}")

    # The name is only for the app/died payload, so an already-freed slot just
    # means an empty one.
    dead = _get_app_info(pid)
    dead_name = dead ? dead[:name].to_s : ""

    # It never came up (a compile error is the usual reason): take the
    # indicator down, and drop any fullscreen request made for it -- pids are
    # slot indices and are handed out again.
    clear_starting(pid)
    take_spawn_fullscreen(pid)

    # Stop any APU voices the app was holding. An app that ends normally does
    # this itself in on_destroy, but one that dies on an exception never gets
    # there and the note would sound forever.
    silence_notes_for(pid)

    # Same for a song it started: the player on the audio side keeps going on
    # its own, and the next app to play music would have to share the APU
    # instance with it.
    stop_music_for(pid)

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

    # Same reason for both of these: a subscriber that reacts by spawning
    # something needs the slot already free.
    announce_app_died(pid, dead_name, expected)
    note_service_host_death(pid, expected)

    # Force immediate check (window list update after task deletion)
    mark_window_list_dirty
    check_terminated_apps
  end

  # Tell whoever is listening that an app ended, and whether it was asked to.
  # The first system topic (ideas.md 6): the kernel publishes, and the service
  # host subscribes so an "app =" entry with restart = true can come back.
  # Published straight rather than through the publish handler, because the
  # kernel is the publisher here and has no pid to send from.
  def announce_app_died(pid, name, expected)
    subs = @subscriptions[DIED_TOPIC]
    return nil unless subs
    return nil if subs.size == 0
    fwd = { "cmd" => "topic_data", "topic" => DIED_TOPIC, "src_pid" => 0,
            "data" => { "pid" => pid, "name" => name,
                        "expected" => expected ? true : false } }
    binary = MessagePack.pack(fwd)
    i = 0
    while i < subs.size
      _send_raw_message(subs[i], FmrbConst::MSG_TYPE_APP_CONTROL, binary)
      i += 1
    end
    nil
  end

  # Decide whether the service host should be started again.
  #
  # Only an unasked-for death counts. A kill, a stop, or the host ending
  # itself is what the user wanted, and bringing it back a second later would
  # make kill meaningless -- which is worse than not restarting at all.
  def note_service_host_death(pid, expected)
    return nil unless pid == @service_host_pid
    @service_host_pid = 0
    if expected
      Log.info("Service host stopped on request; not restarting")
      @service_crashes = []
      @service_respawn_at = 0
      return nil
    end

    now = Machine.board_millis
    kept = []
    i = 0
    while i < @service_crashes.size
      t = @service_crashes[i]
      kept << t if now - t < SERVICE_CRASH_WINDOW_MS
      i += 1
    end
    kept << now
    @service_crashes = kept
    if kept.size >= SERVICE_CRASH_LIMIT
      # Said once and then silence: a host that cannot stay up is a bug to
      # read about in the log, not something to keep respawning.
      Log.error("services host crashed #{SERVICE_CRASH_LIMIT} times, giving up")
      @service_respawn_at = 0
      return nil
    end
    @service_respawn_at = now + SERVICE_RESPAWN_DELAY_MS
    Log.warn("Service host died unexpectedly (#{kept.size}); restarting in #{SERVICE_RESPAWN_DELAY_MS} ms")
    nil
  end

  # Called from the tick. 0 means nothing pending (rather than nil, so the
  # ivar has one type for Spinel).
  def tick_service_respawn
    at = @service_respawn_at
    return nil if at == 0
    return nil if Machine.board_millis < at
    @service_respawn_at = 0
    Log.info("Restarting the service host")
    spawn_service_host
    nil
  end

  def check_terminated_apps
    # Get current window list and check for changes
    old_count = @window_list.size
    update_window_list
    new_count = @window_list.size

    if old_count != new_count
      Log.debug("Periodic cleanup: window list changed (#{old_count} -> #{new_count})")

      # Check if any tracked PIDs are no longer in the window list.
      # Asked directly of the window list rather than through a mapped Array
      # and include?: the map allocates a fresh Array on every change and
      # include? is Enumerable's Ruby one (~1.7 ms per call at four entries).

      # Clean up HID target if it's gone. The window list only holds apps that
      # already reached RUNNING, so an app that was just handed the keyboard can
      # still be in INIT and absent from it -- checking the list alone used to
      # clear the target of a perfectly good app, and every hotkey (Ctrl+Q
      # included) then went nowhere. The context lookup knows about INIT.
      if @hid_target_pid && !window_has_pid?(@hid_target_pid) &&
         !_get_app_info(@hid_target_pid)
        Log.info("HID target app #{@hid_target_pid} no longer exists")
        @hid_target_pid = nil
        _set_hid_target(0xFF)
      end

      # Clean up capture if it's gone
      if @capture_pid && !window_has_pid?(@capture_pid)
        Log.info("Captured app #{@capture_pid} no longer exists")
        @capture_pid = nil
        @capture_mode = nil
      end

      # Clean up mouse_down if it's gone
      if @mouse_down_pid && !window_has_pid?(@mouse_down_pid)
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

    # Take down a start indicator whose app never reported itself started.
    tick_starting

    # Bring a parked fullscreen app back once the windowed app it ran is gone.
    flush_pending_unpark

    # Keep the taskbar's focus marker honest.
    tick_focus_notify

    # Bring the service host back after an unasked-for death.
    tick_service_respawn

    # Periodic cleanup check for terminated apps
    if @tick_count - @last_cleanup_tick >= @cleanup_interval
      check_terminated_apps
      @last_cleanup_tick = @tick_count
    end
  end
end
