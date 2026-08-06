# Family mruby OS - Kernel Main Loop
# This is the core of the Family mruby OS, running at 60Hz
#
# Modules (loaded from fmrb_kernel/ subdirectory):
#   WindowManagerMixin  - Window list, hit testing, z-order
#   InputRouterMixin    - HID event routing and coordinate transformation
#   AppLifecycleMixin   - Fullscreen, suspend/resume, app termination
#   AudioHandlerMixin   - Audio message forwarding to host task

# Per-message trace logging on the kernel's hot paths (message receive,
# audio forward). Off by default: even when the C log level discards the
# line, the Ruby-side string interpolation runs and allocates (~213B per
# message -- at 300 MIDI msg/s that alone forced several GC collections per
# second; see doc/spinel_aot/spinel_gc_notes.md). Flip to true only while
# debugging message flow.
KERNEL_TRACE = false

# FmrbKernelImpl extends FmrbKernel with application-level kernel logic
class FmrbKernelImpl < FmrbKernel
  include WindowManagerMixin
  include InputRouterMixin
  include AppLifecycleMixin
  include AudioHandlerMixin

  # Constants
  MIN_WINDOW_WIDTH = 50
  MIN_WINDOW_HEIGHT = 50

  def initialize()
    Log.info("initialize")
    @app_list = []
    @window_order = []
    @window_list = []

    # HID (input) target tracking
    @hid_target_pid = nil  # Current HID target (focused window)

    # Mouse button state (for click event routing)
    @mouse_down_pid = nil  # Window where mouse_down occurred

    # Mouse capture state (for drag and resize only)
    @capture_pid = nil     # Window that captured mouse
    @capture_mode = nil    # :drag, :resize, or nil

    # Drag state (only valid when @capture_mode == :drag)
    @drag_offset_x = 0
    @drag_offset_y = 0

    # Resize state (only valid when @capture_mode == :resize)
    @resize_start_width = 0
    @resize_start_height = 0
    @resize_start_x = 0
    @resize_start_y = 0

    # Window list optimization
    @window_list_dirty = true

    # Desktop overlay state (dropdown menu)
    @desktop_overlay_active = false
    @desktop_overlay_rect = { x: 0, y: 0, w: 0, h: 0 }
    @desktop_pid = nil

    # Fullscreen mode state
    @fullscreen_pid = nil
    @suspended_pids = []

    # PUB/SUB topic subscriptions: {"topic" => [pid1, pid2, ...]}
    @subscriptions = {}

    # Periodic cleanup tracking
    @tick_count = 0
    @last_cleanup_tick = 0
    @cleanup_interval = 10  # Check every 10 ticks (about 167ms at 60Hz)

    # HID latency stats, reported every 1000 events (engine comparison;
    # board_millis is ms-resolution, so spikes show up in max and the
    # threshold counters rather than in the sum)
    @hid_lat_n = 0
    @hid_lat_sum = 0
    @hid_lat_max = 0
    @hid_lat_ge1 = 0
    @hid_lat_ge5 = 0
    @hid_lat_ge10 = 0
    @hid_lat_gt25 = 0

    _init
    Log.info("Tick = #{@tick}")
    Log.info("Max App Number = #{@max_app_num}")
    _set_ready
  end

  # Maximum non-blocking messages drained per tick after the first, so a flood
  # (e.g. 100 Hz mouse move) cannot starve tick_process. Matches the old _spin
  # "drain multiple messages per tick" behavior with a bound.
  MSG_DRAIN_BURST = 64

  def msg_handler(msg) # called from main_loop (control inversion)
    Log.debug("Received message: type=#{msg[:type]}, src_pid=#{msg[:src_pid]}, data_size=#{msg[:data].length}") if KERNEL_TRACE

    case msg[:type]
    when FmrbConst::MSG_TYPE_APP_CONTROL
      handle_app_control(msg)
    when FmrbConst::MSG_TYPE_HID_EVENT
      handle_hid_event(msg)
    when FmrbConst::MSG_TYPE_APP_GFX
      Log.debug("Graphics message (not implemented)")
    when FmrbConst::MSG_TYPE_APP_AUDIO
      handle_audio_message(msg)
    else
      Log.warn("Unknown message type: #{msg[:type]}")
    end
  end

  # Why the last _spawn_app_req failed, as a sentence for the user. The code is
  # the fmrb_err.h value the C spawner returned (@last_spawn_err, set by both
  # engines' bindings). An unmapped code keeps its number rather than having a
  # cause invented for it -- the dialog used to blame a missing .toml for every
  # failure, which sent us looking in the wrong place twice.
  def spawn_error_reason
    case @last_spawn_err
    when -9  # FMRB_ERR_NO_RESOURCE
      "No free app slot. Close a running app and try again."
    when -7  # FMRB_ERR_NOT_FOUND
      "The file is missing, or it is a name no built-in app has."
    when -2  # FMRB_ERR_NO_MEMORY
      "Not enough memory to start it."
    when -1  # FMRB_ERR_INVALID_PARAM
      "The path is not usable (too long, or empty)."
    else
      "Spawn failed (error=#{@last_spawn_err})."
    end
  end

  def handle_app_control(msg)
    data_binary = msg[:data]
    pid = msg[:src_pid]

    # Deserialize msgpack data
    begin
      data = MessagePack.unpack(data_binary)
    rescue => e
      Log.error("Failed to unpack msgpack data: #{e}")
      return
    end

    # Data should be a Hash with "cmd" key (use strings, not symbols for VM-to-VM communication)
    unless data.is_a?(Hash) && data.key?("cmd")
      Log.error("Invalid app control message format (expected Hash with 'cmd')")
      Log.error("Received: #{data.inspect}")
      return
    end

    cmd = data["cmd"]

    case cmd
    when "spawn"
      app_name = data["app_name"] || data["app"] || ""
      Log.info("Spawn request from pid=#{pid}: #{app_name}")

      new_pid = _spawn_app_req(app_name)
      if new_pid
        Log.info("App #{app_name} spawned successfully with PID #{new_pid}")
        after_spawn(new_pid)
      else
        Log.error("Failed to spawn app: #{app_name}")
        # Let the desktop show an error dialog to the user
        if @desktop_pid
          reason = spawn_error_reason
          data = MessagePack.pack({"cmd" => "spawn_failed", "app" => app_name,
                                   "reason" => reason})
          _send_raw_message(@desktop_pid, FmrbConst::MSG_TYPE_APP_CONTROL, data)
        end
      end
    when "spawned"
      # A spawn that did not come through this handler: the debug daemon (and
      # anything else on the C API) calls fmrb_app_spawn_app directly, and the
      # app then came up without the keyboard. The C side reports the new pid
      # here so it gets the same follow-up. Skipped when this handler already
      # did it for that pid.
      new_pid = data["pid"]
      if new_pid && new_pid != @hid_target_pid
        Log.info("Spawn notification for pid=#{new_pid} (spawned outside the kernel)")
        after_spawn(new_pid)
      end
    when "run"
      # Editor RUN (F5): run a file, replacing the instance the requester
      # started last time.
      run_path = (data["path"] || "").to_s
      prev_pid = data["prev_pid"]
      prev_info = prev_pid ? _get_app_info(prev_pid) : nil
      # Only stop something that is still a user app: a pid the requester ran
      # earlier may have exited and had its slot taken by an unrelated app, and
      # a requester must never be able to stop the desktop or itself this way.
      prev_stoppable = prev_info && prev_pid != pid && run_path_allowed?(prev_info[:path].to_s)

      # Kept in a local rather than negated in place: Spinel's codegen emits the
      # call's hoisted temporaries inside the if condition when a call sits
      # under a unary "!", which does not compile (ruby_writing_constraints B).
      run_allowed = run_path_allowed?(run_path)
      if !run_allowed
        Log.warn("Run request from pid=#{pid} rejected: #{run_path}")
        reply_run_result(pid, run_path, nil)
      elsif prev_stoppable
        # Stop the old instance and spawn from the exit notification, so its
        # memory pool is released before the new instance asks for one.
        Log.info("Run: stopping PID #{prev_pid} first, then #{run_path}")
        @pending_reload = { path: run_path, requester: pid }
        stop_data = MessagePack.pack({ "cmd" => "stop" })
        _send_raw_message(prev_pid, FmrbConst::MSG_TYPE_APP_CONTROL, stop_data)
      else
        new_pid = _spawn_app_req(run_path)
        if new_pid
          Log.info("Run: spawned #{run_path} as PID #{new_pid}")
          after_spawn(new_pid)
        else
          Log.error("Run: failed to spawn #{run_path}: #{spawn_error_reason}")
        end
        reply_run_result(pid, run_path, new_pid)
      end
    when "exit"
      Log.info("App exit notification from pid=#{pid}")
      cleanup_terminated_app(pid)
    when "file_select"
      # Forward file select request to desktop
      if @desktop_pid
        @file_select_requester = pid
        @file_select_prev_hid_target = @hid_target_pid
        # Redirect keyboard input to desktop for filename entry
        _set_hid_target(@desktop_pid)
        @hid_target_pid = @desktop_pid
        fwd = { "cmd" => "file_select", "mode" => data["mode"] || "open", "requester_pid" => pid }
        binary = MessagePack.pack(fwd)
        _send_raw_message(@desktop_pid, FmrbConst::MSG_TYPE_APP_CONTROL, binary)
        Log.info("File select request from pid=#{pid} forwarded to desktop, HID -> desktop")
      end
    when "file_select_result"
      # Forward result back to requester app
      target = data["target_pid"] || @file_select_requester
      if target
        result = { "cmd" => "file_selected", "path" => data["path"], "mode" => data["mode"] }
        binary = MessagePack.pack(result)
        _send_raw_message(target, FmrbConst::MSG_TYPE_APP_CONTROL, binary)
        Log.info("File select result: path=#{data["path"]} -> pid=#{target}")
        # Restore HID target
        if @file_select_prev_hid_target
          _set_hid_target(@file_select_prev_hid_target)
          @hid_target_pid = @file_select_prev_hid_target
          Log.info("HID target restored to pid=#{@file_select_prev_hid_target}")
        end
        @file_select_requester = nil
        @file_select_prev_hid_target = nil
      end
    when "overlay_state"
      @desktop_overlay_active = data["active"] || false
      @desktop_overlay_rect = {
        x: data["rect_x"] || 0, y: data["rect_y"] || 0,
        w: data["rect_w"] || 0, h: data["rect_h"] || 0
      }
      Log.info("Desktop overlay: active=#{@desktop_overlay_active}")
    when "system_interrupt"
      # Reserved key (Ctrl-Q, intercepted in host_task before routing so the app
      # never sees it): close the app that owns the keyboard. Without this a
      # fullscreen .bas cannot be left at all -- every key goes to it and it
      # covers the screen.
      #
      # Only apps started from /app or /home are stopped. Built-ins have no path
      # and are left alone: the editor owns an unsaved-changes dialog and must
      # not be stopped from under the user. For those, fall back to the older
      # behaviour of just leaving fullscreen.
      Log.info("System interrupt (Ctrl+Q)")
      target = @hid_target_pid
      info = target ? _get_app_info(target) : nil
      if info && run_path_allowed?(info[:path].to_s)
        Log.info("Ctrl-Q: stopping pid=#{target} (#{info[:path]})")
        stop_data = MessagePack.pack({ "cmd" => "stop" })
        _send_raw_message(target, FmrbConst::MSG_TYPE_APP_CONTROL, stop_data)
      elsif @fullscreen_pid
        exit_fullscreen
      end
    when "reload_confirm"
      # App requests reload confirmation via desktop dialog
      info = _get_app_info(pid)
      if info && info[:load_mode] == 1 && info[:path]
        app_name = info[:name] || "App"
        # Ask desktop to show confirm dialog
        if @desktop_pid
          fwd = {
            "cmd" => "confirm_dialog",
            "message" => "Reload #{app_name}?",
            "on_yes_cmd" => "reload",
            "reload_pid" => pid,
            "reload_path" => info[:path]
          }
          binary = MessagePack.pack(fwd)
          _send_raw_message(@desktop_pid, FmrbConst::MSG_TYPE_APP_CONTROL, binary)
        end
      end
    when "reload"
      # Confirmed reload: stop app and re-spawn
      reload_pid = data["reload_pid"]
      reload_path = data["reload_path"]
      if reload_pid && reload_path
        Log.info("Reload: stopping PID #{reload_pid}, will respawn #{reload_path}")
        @pending_reload = { path: reload_path }
        # Send stop to the target app
        stop_data = MessagePack.pack({ "cmd" => "clear_and_stop" })
        _send_raw_message(reload_pid, FmrbConst::MSG_TYPE_APP_CONTROL, stop_data)
      end
    when "app_error"
      err = _get_last_error
      if err
        Log.error("App '#{err[:name]}': #{err[:error]}")
        if @desktop_pid
          fwd = { "cmd" => "show_error" }
          binary = MessagePack.pack(fwd)
          _send_raw_message(@desktop_pid, FmrbConst::MSG_TYPE_APP_CONTROL, binary)
        end
      end
    when "focus_app"
      target_pid = data["pid"]
      if target_pid
        _bring_to_front(target_pid)
        _set_hid_target(target_pid)
        @hid_target_pid = target_pid
        mark_window_list_dirty
        Log.info("Focus switched to PID #{target_pid} (requested by pid=#{pid})")
      end
    when "kill"
      Log.info("Kill request from pid=#{pid} (not implemented)")
      # TODO: Implement kill command to forcefully terminate app
    when "subscribe"
      topic = data["topic"]
      if topic
        # (Spinel has no `hash[k] ||= v` / IndexOrWriteNode; use an explicit form)
        @subscriptions[topic] = [] unless @subscriptions[topic]
        @subscriptions[topic] << pid unless @subscriptions[topic].include?(pid)
        Log.info("PID #{pid} subscribed to '#{topic}'")
      end
    when "unsubscribe"
      topic = data["topic"]
      subs = @subscriptions[topic]
      if topic && subs
        # Rebuild without pid instead of Array#delete: subs is a poly hash value,
        # and Spinel mis-dispatches poly-receiver .delete to String#delete.
        kept = []
        subs.each { |sp| kept << sp unless sp == pid }
        if kept.empty?
          @subscriptions.delete(topic)
        else
          @subscriptions[topic] = kept
        end
        Log.info("PID #{pid} unsubscribed from '#{topic}'")
      end
    when "publish"
      topic = data["topic"]
      payload = data["data"]
      subscribers = @subscriptions[topic]
      if subscribers && subscribers.length > 0
        fwd = {"cmd" => "topic_data", "topic" => topic, "data" => payload, "src_pid" => pid}
        binary = MessagePack.pack(fwd)
        subscribers.each do |sub_pid|
          next if sub_pid == pid
          _send_raw_message(sub_pid, FmrbConst::MSG_TYPE_APP_CONTROL, binary)
        end
      end
    when "reboot"
      Log.info("Reboot requested by PID #{pid}; forwarding to desktop")
      # Desktop VM has FmrbApp.reboot available; route there to perform the
      # actual esp_restart/exit. Keeping the kernel VM agnostic to FmrbApp.
      if @desktop_pid
        fwd = { "cmd" => "do_reboot" }
        binary = MessagePack.pack(fwd)
        _send_raw_message(@desktop_pid, FmrbConst::MSG_TYPE_APP_CONTROL, binary)
      end
    when "suspend"
      Log.info("Suspend request (not implemented)")
    when "resume"
      Log.info("Resume request (not implemented)")
    else
      Log.warn("Unknown app control command: #{cmd}")
    end
  end

  def main_loop
    Log.info("main_loop started")
    while true
      tick_process
      drain_messages(@tick)
    end
  end

  # Poll-based message drain (control inversion): block up to budget_ms for the
  # first message, then drain any queued burst non-blocking (bounded). Replaces
  # the C _spin, so the mruby and Spinel kernels share this loop. Each dispatch
  # is guarded so one bad message logs and continues instead of killing the VM
  # (the old _spin caught per-message exceptions in C).
  def drain_messages(budget_ms)
    msg = _poll_message(budget_ms)
    return unless msg
    dispatch_message(msg)
    n = 0
    while n < MSG_DRAIN_BURST
      m = _poll_message(0)
      break unless m
      dispatch_message(m)
      n += 1
    end
  end

  def dispatch_message(msg)
    begin
      msg_handler(msg)
    rescue => e
      Log.error("Error in msg_handler: #{e.class}: #{e.message}")
    end
  end

  def sync_files
    files = _get_sync_files
    if files.empty?
      Log.info("No files to sync")
      return
    end
    Log.info("File sync: #{files.size} file(s) configured")
    files.each_with_index do |entry, i|
      result = _sync_file(entry[:src], entry[:dest])
      if result
        Log.info("File sync [#{i}]: #{entry[:src]} synced")
      else
        Log.warn("File sync [#{i}]: #{entry[:src]} failed or not found")
      end
    end
    Log.info("File sync complete")
  end

  # The clock itself is set in C during boot (main/kernel/fmrb_rtc.c), so all
  # that is left here is telling the graphics side what time it is.
  #
  # It used to read the RTC through picoruby's I2C and RX8900 / RX8130 classes,
  # which the Spinel build has no way to resolve, so the whole block sat behind
  # a spinel-strip marker on the assumption that the platform check above always
  # returns first. On the device it does not: PLATFORM is "esp32", the check
  # passes, and what followed had been stripped -- so the Spinel kernel silently
  # left the clock at the epoch. Reading it in C removes the engine from the
  # question entirely.
  def sync_rtc
    unless FmrbConst::PLATFORM == "esp32"
      Log.info("RTC sync: skipped (not ESP32)")
      return
    end
    _sync_time_to_host
    Log.info("RTC sync: time sent to host")
  end

  def initial_sequence
    # Check protocol version with host (retry up to 3 times for startup timing)
    version_ok = false
    attempt = 0
    while attempt < 3
      Log.info("Checking Protocol version... (attempt #{attempt + 1}/3)")
      if check_protocol_version(5000)
        version_ok = true
        break
      end
      Log.warn("Protocol version check attempt #{attempt + 1} failed, retrying...")
      Machine.delay_ms(500)
      attempt += 1
    end
    unless version_ok
      Log.error("ERROR: Protocol version check failed after 3 attempts")
      _set_error_led(FmrbConst::LED_ERR_VERSION_MISMATCH)
      raise "Protocol version check failed"
    end
    Log.info("Protocol version check passed")

    # Check GA firmware version (retry up to 3 times)
    ga_ok = false
    attempt = 0
    while attempt < 3
      Log.info("Checking GA version... (attempt #{attempt + 1}/3)")
      if check_ga_version(5000)
        ga_ok = true
        break
      end
      Log.warn("GA version check attempt #{attempt + 1} failed, retrying...")
      Machine.delay_ms(500)
      attempt += 1
    end
    unless ga_ok
      Log.error("ERROR: GA version check failed after 3 attempts")
      _set_error_led(FmrbConst::LED_ERR_VERSION_MISMATCH)
      raise "GA version check failed"
    end
    Log.info("GA version check passed")

    # Sync files to host (after protocol version confirmed)
    sync_files

    # Sync RTC to system clock (ESP32 only)
    sync_rtc

    # Spawn system desktop app (bg canvas z=0, fg canvas z=254)
    desktop_pid = _spawn_app_req("system/desktop")
    if desktop_pid
      @desktop_pid = desktop_pid
      _set_hid_target(desktop_pid)
      @hid_target_pid = desktop_pid
    else
      Log.error("Failed to spawn system desktop app")
    end
  end

  def start
    initial_sequence
    main_loop
  end
end

# Start the kernel
Log.info "Family mruby OS Kernel starting..."

begin
  kernel = FmrbKernelImpl.new
  Log.info "Kernel created successfully"
  kernel.start
rescue => e
  Log.error "Exception caught: #{e.class}"
  Log.error "Message: #{e.message}"
  Log.error "Backtrace:"
  Log.error e.backtrace.join("\n") if e.backtrace
end

Log.info "Family mruby OS Kernel exit"
