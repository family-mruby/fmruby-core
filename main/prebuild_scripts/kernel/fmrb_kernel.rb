# Family mruby OS - Kernel Main Loop
# This is the core of the Family mruby OS, running at 60Hz

# FmrbKernelImpl extends FmrbKernel with application-level kernel logic
class FmrbKernelImpl < FmrbKernel
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

    _init
    Log.info("Tick = #{@tick}")
    Log.info("Max App Number = #{@max_app_num}")
    _set_ready
  end

  def msg_handler(msg) # called from _spin
    Log.debug("Received message: type=#{msg[:type]}, src_pid=#{msg[:src_pid]}, data_size=#{msg[:data].length}")

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

        # Mark window list as dirty (new window created)
        mark_window_list_dirty

        # Check if fullscreen app after a short delay for init to complete
        # Schedule check in tick_process instead of immediate
        @pending_fullscreen_check = new_pid

        # Set HID target to the newly spawned app
        _set_hid_target(new_pid)
        @hid_target_pid = new_pid  # Track HID target
        Log.info("HID target set to new app pid=#{new_pid}")
      else
        Log.error("Failed to spawn app: #{app_name}")
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
      Log.info("System interrupt (Ctrl+Q)")
      if @fullscreen_pid
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
    when "kill"
      Log.info("Kill request from pid=#{pid} (not implemented)")
      # TODO: Implement kill command to forcefully terminate app
    when "subscribe"
      topic = data["topic"]
      if topic
        @subscriptions[topic] ||= []
        @subscriptions[topic] << pid unless @subscriptions[topic].include?(pid)
        Log.info("PID #{pid} subscribed to '#{topic}'")
      end
    when "unsubscribe"
      topic = data["topic"]
      if topic && @subscriptions[topic]
        @subscriptions[topic].delete(pid)
        @subscriptions.delete(topic) if @subscriptions[topic].empty?
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
    when "suspend"
      Log.info("Suspend request (not implemented)")
    when "resume"
      Log.info("Resume request (not implemented)")
    else
      Log.warn("Unknown app control command: #{cmd}")
    end
  end

  def handle_audio_message(msg)
    data_binary = msg[:data]
    pid = msg[:src_pid]

    begin
      data = MessagePack.unpack(data_binary)
    rescue => e
      Log.error("Failed to unpack audio message: #{e}")
      return
    end

    unless data.is_a?(Hash) && data.key?("cmd")
      Log.error("Invalid audio message format")
      return
    end

    cmd = data["cmd"]
    Log.info("Audio command '#{cmd}' from pid=#{pid}")

    # Forward audio command to host task as raw binary
    # Format: cmd_type(1) + path_len(2, LE) + path
    case cmd
    when "play"
      path = data["path"] || ""
      # Build binary: cmd_type=0x02 (PLAY) + path_len(2 bytes LE) + path
      path_len = path.length
      bin = "\x02\x00\x00" + path
      bin.setbyte(1, path_len & 0xFF)
      bin.setbyte(2, (path_len >> 8) & 0xFF)
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, bin)
    when "stop"
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, "\x03")
    when "pause"
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, "\x04")
    when "resume"
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, "\x05")
    when "load_fmsq"
      slot = data["slot"] || 0
      fmsq_data = data["data"] || ""
      # Build binary: cmd_type=0x01 (LOAD_BINARY) + music_id(4 LE) + data_size(4 LE) + data
      data_len = fmsq_data.length
      bin = "\x01\x00\x00\x00\x00\x00\x00\x00\x00" + fmsq_data
      bin.setbyte(1, slot & 0xFF)
      bin.setbyte(2, (slot >> 8) & 0xFF)
      bin.setbyte(3, (slot >> 16) & 0xFF)
      bin.setbyte(4, (slot >> 24) & 0xFF)
      bin.setbyte(5, data_len & 0xFF)
      bin.setbyte(6, (data_len >> 8) & 0xFF)
      bin.setbyte(7, (data_len >> 16) & 0xFF)
      bin.setbyte(8, (data_len >> 24) & 0xFF)
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, bin)
    when "play_slot"
      slot = data["slot"] || 0
      # Build binary: cmd_type=0x08 (PLAY_SLOT) + music_id(4 LE)
      bin = "\x08\x00\x00\x00\x00"
      bin.setbyte(1, slot & 0xFF)
      bin.setbyte(2, (slot >> 8) & 0xFF)
      bin.setbyte(3, (slot >> 16) & 0xFF)
      bin.setbyte(4, (slot >> 24) & 0xFF)
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, bin)
    when "note_on"
      ch = data["ch"] || 0
      freq = data["freq"] || 440
      vol = data["vol"] || 10
      duty = data["duty"] || 2
      sweep = data["sweep"] || 0
      # Build binary: cmd_type=0x09 + ch(1) + freq(2 LE) + vol(1) + duty(1) + sweep(1)
      bin = "\x09\x00\x00\x00\x00\x00\x00"
      bin.setbyte(1, ch & 0xFF)
      bin.setbyte(2, freq & 0xFF)
      bin.setbyte(3, (freq >> 8) & 0xFF)
      bin.setbyte(4, vol & 0xFF)
      bin.setbyte(5, duty & 0xFF)
      bin.setbyte(6, sweep & 0xFF)
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, bin)
    when "note_off"
      ch = data["ch"] || 0
      # Build binary: cmd_type=0x0A + ch(1)
      bin = "\x0A\x00"
      bin.setbyte(1, ch & 0xFF)
      _send_raw_message(FmrbConst::PROC_ID_HOST, FmrbConst::MSG_TYPE_APP_AUDIO, bin)
    else
      Log.warn("Unknown audio command: #{cmd}")
    end
  end

  def handle_hid_event(msg)
    data_binary = msg[:data]
    src_pid = msg[:src_pid]

    # Parse HID event data (binary format)
    # Format: subtype(1 byte) + button(1 byte) + x(2 bytes) + y(2 bytes)
    return if data_binary.size < 6

    subtype = data_binary.getbyte(0)
    button = data_binary.getbyte(1)
    # Little endian: low byte first, high byte second
    x = data_binary.getbyte(2) | (data_binary.getbyte(3) << 8)
    y = data_binary.getbyte(4) | (data_binary.getbyte(5) << 8)

    begin
      case subtype
      when 4  # Mouse button down
        update_window_list(true)  # Show log on click
        target_window = find_window_at(x, y)

        if target_window.nil?
          # Fallback to desktop (click on empty area)
          if @desktop_pid
            target_window = find_window_by_pid(@desktop_pid)
          end
          if target_window.nil?
            Log.info("No window at (#{x},#{y})")
            return
          end
        end

        target_pid = target_window[:pid]
        target_name = target_window[:app_name]
        target_z = target_window[:z_order]
        win_x = target_window[:x]
        win_y = target_window[:y]
        win_width = target_window[:width]
        win_height = target_window[:height]

        Log.info("Click at (#{x},#{y}) -> '#{target_name}' (PID #{target_pid}, Z=#{target_z})")

        # Bring clicked window to front
        _bring_to_front(target_pid)
        _set_hid_target(target_pid)
        @hid_target_pid = target_pid  # Track HID target
        mark_window_list_dirty  # Z-order changed

        # Calculate relative position within window
        relative_x = x - win_x
        relative_y = y - win_y

        Log.info("Relative pos in window: (#{relative_x},#{relative_y}), size=#{win_width}x#{win_height}")

        # Record mouse_down window for button_up event routing
        @mouse_down_pid = target_pid

        # Check for resize handle (bottom-right 10x10 area) first
        # Only for resizable windows
        if target_window[:resizable] &&
           relative_x >= win_width - 10 && relative_y >= win_height - 10
          # Start resize and capture mouse
          @capture_mode = :resize
          @capture_pid = target_pid
          @resize_start_width = win_width
          @resize_start_height = win_height
          @resize_start_x = x
          @resize_start_y = y
          Log.info("Start resize: PID #{target_pid}, size=(#{win_width}x#{win_height})")
        # Check if click is in menu bar region (not resizing and not close button)
        elsif target_name != "system_desktop" && target_name != "system_overlay" && relative_y < 11 && relative_x < win_width - 10
          # Start drag and capture mouse (excluding close button area on the right)
          @capture_mode = :drag
          @capture_pid = target_pid
          @drag_offset_x = x - win_x
          @drag_offset_y = y - win_y
          Log.info("Start drag: PID #{target_pid}, offset=(#{@drag_offset_x},#{@drag_offset_y})")
        end

        # Create new binary message with relative coordinates
        # Format: subtype(1 byte) + button(1 byte) + x(2 bytes) + y(2 bytes)
        relative_data = "\x00\x00\x00\x00\x00\x00"
        relative_data.setbyte(0, subtype)
        relative_data.setbyte(1, button)
        relative_data.setbyte(2, relative_x & 0xFF)        # x low byte
        relative_data.setbyte(3, (relative_x >> 8) & 0xFF) # x high byte
        relative_data.setbyte(4, relative_y & 0xFF)        # y low byte
        relative_data.setbyte(5, (relative_y >> 8) & 0xFF) # y high byte

        # Forward event with relative coordinates to the clicked window
        _send_raw_message(target_pid, FmrbConst::MSG_TYPE_HID_EVENT, relative_data)

      when 3  # Mouse move
        # Handle drag/resize operations based on @capture_mode
        if @capture_mode == :resize && @capture_pid
          # Calculate new window size with constraints
          new_width = @resize_start_width + (x - @resize_start_x)
          new_height = @resize_start_height + (y - @resize_start_y)

          # Apply minimum size constraints
          new_width = MIN_WINDOW_WIDTH if new_width < MIN_WINDOW_WIDTH
          new_height = MIN_WINDOW_HEIGHT if new_height < MIN_WINDOW_HEIGHT

          # Update window size
          if _update_window_size(@capture_pid, new_width, new_height)
            mark_window_list_dirty  # Size changed
          else
            Log.info("Failed to update window size")
            # Release capture on error
            @capture_pid = nil
            @capture_mode = nil
          end

        elsif @capture_mode == :drag && @capture_pid
          # Calculate new window position
          new_x = x - @drag_offset_x
          new_y = y - @drag_offset_y

          # Update window position
          if _update_window_position(@capture_pid, new_x, new_y)
            mark_window_list_dirty  # Position changed
          else
            Log.info("Failed to update window position")
            # Release capture on error
            @capture_pid = nil
            @capture_mode = nil
          end
        end

        # Forward mouse_move event:
        # - If capture is active (drag/resize): send to @capture_pid
        # - Otherwise: send to @hid_target_pid (focused window)
        target_pid = @capture_pid || @hid_target_pid
        if target_pid
          # Convert to window-relative coordinates
          target_window = find_window_by_pid(target_pid)
          if target_window
            relative_x = x - target_window[:x]
            relative_y = y - target_window[:y]

            # Create new binary message with relative coordinates
            relative_data = "\x00\x00\x00\x00\x00\x00"
            relative_data.setbyte(0, subtype)
            relative_data.setbyte(1, button)
            relative_data.setbyte(2, relative_x & 0xFF)        # x low byte
            relative_data.setbyte(3, (relative_x >> 8) & 0xFF) # x high byte
            relative_data.setbyte(4, relative_y & 0xFF)        # y low byte
            relative_data.setbyte(5, (relative_y >> 8) & 0xFF) # y high byte

            _send_raw_message(target_pid, FmrbConst::MSG_TYPE_HID_EVENT, relative_data)
          end
        end

      when 5  # Mouse button up
        # Forward to captured window or mouse_down window
        target_pid = @capture_pid || @mouse_down_pid
        if target_pid
          # Convert to window-relative coordinates
          target_window = find_window_by_pid(target_pid)
          if target_window
            relative_x = x - target_window[:x]
            relative_y = y - target_window[:y]

            # Create new binary message with relative coordinates
            relative_data = "\x00\x00\x00\x00\x00\x00"
            relative_data.setbyte(0, subtype)
            relative_data.setbyte(1, button)
            relative_data.setbyte(2, relative_x & 0xFF)        # x low byte
            relative_data.setbyte(3, (relative_x >> 8) & 0xFF) # x high byte
            relative_data.setbyte(4, relative_y & 0xFF)        # y low byte
            relative_data.setbyte(5, (relative_y >> 8) & 0xFF) # y high byte

            _send_raw_message(target_pid, FmrbConst::MSG_TYPE_HID_EVENT, relative_data)
          end
        end

        # Release capture and reset state based on @capture_mode
        if @capture_mode == :resize
          Log.info("End resize: PID #{@capture_pid}")
          @resize_start_width = 0
          @resize_start_height = 0
          @resize_start_x = 0
          @resize_start_y = 0
        elsif @capture_mode == :drag
          Log.info("End drag: PID #{@capture_pid}")
          @drag_offset_x = 0
          @drag_offset_y = 0
        end

        # Clear all mouse button state
        @capture_pid = nil
        @capture_mode = nil
        @mouse_down_pid = nil
      end

    rescue => e
      Log.error("Error in handle_hid_event: #{e.class}: #{e.message}")
    end
  end

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

  MENU_BAR_HEIGHT = 13

  def find_window_at(x, y)
    # Desktop foreground (z=254) special hit-testing:
    # 1. Menu bar region: always hit-testable
    # 2. Dropdown open: dropdown rect is hit-testable, outside closes it
    # 3. Otherwise: transparent (skip to windows below)
    if @desktop_pid
      if y < MENU_BAR_HEIGHT
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

  def suspend_app(pid)
    data = MessagePack.pack({ "cmd" => "suspend" })
    _send_raw_message(pid, FmrbConst::MSG_TYPE_APP_CONTROL, data)
    Log.info("Suspended app PID #{pid}")
  end

  def resume_app(pid)
    data = MessagePack.pack({ "cmd" => "resume" })
    _send_raw_message(pid, FmrbConst::MSG_TYPE_APP_CONTROL, data)
    Log.info("Resumed app PID #{pid}")
  end

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

  def tick_process
    # Periodic kernel tasks
    @tick_count += 1

    # Deferred fullscreen check (after app init completes)
    if @pending_fullscreen_check
      pid = @pending_fullscreen_check
      @pending_fullscreen_check = nil
      update_window_list(true)
      if is_fullscreen_app?(pid)
        enter_fullscreen(pid)
      end
    end

    # Periodic cleanup check for terminated apps
    if @tick_count - @last_cleanup_tick >= @cleanup_interval
      check_terminated_apps
      @last_cleanup_tick = @tick_count
    end
  end

  def main_loop
    Log.info("main_loop started")
    loop do
      tick_process
      _spin(@tick)
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

  def initial_sequence
    # Check protocol version with host (retry up to 3 times for startup timing)
    version_ok = false
    3.times do |attempt|
      Log.info("Checking protocol version... (attempt #{attempt + 1}/3)")
      if check_protocol_version(5000)
        version_ok = true
        break
      end
      Log.warn("Protocol version check attempt #{attempt + 1} failed, retrying...")
      sleep_ms(500)
    end
    unless version_ok
      Log.error("ERROR: Protocol version check failed after 3 attempts")
      raise "Protocol version mismatch with host"
    end
    Log.info("Protocol version check passed")

    # Sync files to host (after protocol version confirmed)
    sync_files

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
