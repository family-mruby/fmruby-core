# Family mruby OS - Kernel Framework
# Kernel provides system-level services for the Family mruby OS
class AppData
  def initialize()
    @name
    @path
    @visible = true
  end
end

class FmrbKernel
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
      Log.debug("Audio message (not implemented)")
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

        # Set HID target to the newly spawned app
        _set_hid_target(new_pid)
        @hid_target_pid = new_pid  # Track HID target
        Log.info("HID target set to new app pid=#{new_pid}")
      else
        Log.error("Failed to spawn app: #{app_name}")
      end
    when "kill"
      Log.info("Kill request from pid=#{pid} (not implemented)")
      # TODO: Reset HID target if this was the target app
      # _set_hid_target(0xFF)
      # Update window list after kill
      # update_window_list
    when "suspend"
      Log.info("Suspend request (not implemented)")
    when "resume"
      Log.info("Resume request (not implemented)")
    else
      Log.warn("Unknown app control command: #{cmd}")
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
          Log.info("No window at (#{x},#{y})")
          return
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
        # Resize handle only for non-system_gui windows
        if target_name != "system_gui" &&
           relative_x >= win_width - 10 && relative_y >= win_height - 10
          # Start resize and capture mouse
          @capture_mode = :resize
          @capture_pid = target_pid
          @resize_start_width = win_width
          @resize_start_height = win_height
          @resize_start_x = x
          @resize_start_y = y
          Log.info("Start resize: PID #{target_pid}, size=(#{win_width}x#{win_height})")
        # Check if click is in menu bar region (not resizing)
        elsif target_name != "system_gui" && relative_y < 11
          # Start drag and capture mouse
          @capture_mode = :drag
          @capture_pid = target_pid
          @drag_offset_x = x - win_x
          @drag_offset_y = y - win_y
          Log.info("Start drag: PID #{target_pid}, offset=(#{@drag_offset_x},#{@drag_offset_y})")
        end

        # Forward event to the clicked window
        _send_raw_message(target_pid, FmrbConst::MSG_TYPE_HID_EVENT, data_binary)

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
          _send_raw_message(target_pid, FmrbConst::MSG_TYPE_HID_EVENT, data_binary)
        end

      when 5  # Mouse button up
        # Forward to captured window or mouse_down window
        target_pid = @capture_pid || @mouse_down_pid
        if target_pid
          _send_raw_message(target_pid, FmrbConst::MSG_TYPE_HID_EVENT, data_binary)
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

  def find_window_at(x, y)
    # Search from front to back (highest z_order first)
    # Find window with highest z_order that contains the point
    target_window = nil
    max_z_order = -1

    @window_list.each do |win|
      # Use <= for right and bottom edges to include boundary pixels
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

  def tick_process
    # Periodic kernel tasks
  end

  def main_loop
    Log.info("main_loop started")
    loop do
      tick_process
      _spin(@tick)
    end
  end

  def initial_sequence
    # Check protocol version with host
    Log.info("Checking protocol version...")
    unless check_protocol_version(5000)
      Log.error("ERROR: Protocol version check failed")
      raise "Protocol version mismatch with host"
    end
    Log.info("Protocol version check passed")

    # Spawn system GUI app
    gui_pid = _spawn_app_req("system/gui_app")
    if gui_pid
      _set_hid_target(gui_pid)
      @hid_target_pid = gui_pid  # Track HID target
    else
      Log.error("Failed to spawn system GUI app")
    end
  end

  def start
    initial_sequence
    main_loop
  end
end

