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
  def initialize()
    puts "[KERNEL] initialize"
    @app_list = []
    @window_order = []
    @window_list = []

    # Drag and drop state
    @dragging = false
    @drag_target_pid = nil
    @drag_offset_x = 0
    @drag_offset_y = 0

    # Resize state
    @resizing = false
    @resize_target_pid = nil
    @resize_start_width = 0
    @resize_start_height = 0
    @resize_start_x = 0
    @resize_start_y = 0

    _init
    puts "[KERNEL] Tick = #{@tick}"
    puts "[KERNEL] Max App Number = #{@max_app_num}"
    _set_ready
  end

  def msg_handler(msg) # called from _spin
    case msg[:type]
    when FmrbConst::MSG_TYPE_APP_CONTROL
      handle_app_control(msg)
    when FmrbConst::MSG_TYPE_HID_EVENT
      handle_hid_event(msg)
    when FmrbConst::MSG_TYPE_APP_GFX
      puts "[KERNEL] Graphics message (not implemented)"
    when FmrbConst::MSG_TYPE_APP_AUDIO
      puts "[KERNEL] Audio message (not implemented)"
    else
      puts "[KERNEL] Unknown message type: #{msg[:type]}"
    end
  end

  def handle_app_control(msg)
    data_binary = msg[:data]
    pid = msg[:src_pid]

    # Deserialize msgpack data
    begin
      data = MessagePack.unpack(data_binary)
    rescue => e
      puts "[KERNEL] Failed to unpack msgpack data: #{e}"
      return
    end

    # Data should be a Hash with "cmd" key (use strings, not symbols for VM-to-VM communication)
    unless data.is_a?(Hash) && data.key?("cmd")
      puts "[KERNEL] Invalid app control message format (expected Hash with 'cmd')"
      puts "[KERNEL] Received: #{data.inspect}"
      return
    end

    cmd = data["cmd"]

    case cmd
    when "spawn"
      app_name = data["app_name"] || data["app"] || ""
      puts "[KERNEL] Spawn request from pid=#{pid}: #{app_name}"

      result = _spawn_app_req(app_name)
      if result
        puts "[KERNEL] App #{app_name} spawned successfully"

        # Set HID target to the newly spawned app
        _set_hid_target(pid)
        puts "[KERNEL] HID target set to app pid=#{pid}"

        # Update window list
        update_window_list
      else
        puts "[KERNEL] Failed to spawn app: #{app_name}"
      end
    when "kill"
      puts "[KERNEL] Kill request from pid=#{pid} (not implemented)"
      # TODO: Reset HID target if this was the target app
      # _set_hid_target(0xFF)
      # Update window list after kill
      # update_window_list
    when "suspend"
      puts "[KERNEL] Suspend request (not implemented)"
    when "resume"
      puts "[KERNEL] Resume request (not implemented)"
    else
      puts "[KERNEL] Unknown app control command: #{cmd}"
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
          puts "[KERNEL] No window at (#{x},#{y})"
          return
        end

        target_pid = target_window[:pid]
        target_name = target_window[:app_name]
        target_z = target_window[:z_order]
        win_x = target_window[:x]
        win_y = target_window[:y]
        win_width = target_window[:width]
        win_height = target_window[:height]

        puts "[KERNEL] Click at (#{x},#{y}) -> '#{target_name}' (PID #{target_pid}, Z=#{target_z})"

        # Bring clicked window to front
        _bring_to_front(target_pid)
        _set_hid_target(target_pid)

        # Calculate relative position within window
        relative_x = x - win_x
        relative_y = y - win_y

        puts "[KERNEL] Relative pos in window: (#{relative_x},#{relative_y}), size=#{win_width}x#{win_height}"

        # Check for resize handle (bottom-right 10x10 area) first
        # Resize handle only for non-system_gui windows
        if target_name != "system_gui" &&
           relative_x >= win_width - 10 && relative_y >= win_height - 10
          # Start resize
          @resizing = true
          @resize_target_pid = target_pid
          @resize_start_width = win_width
          @resize_start_height = win_height
          @resize_start_x = x
          @resize_start_y = y
          puts "[KERNEL] Start resize: PID #{target_pid}, size=(#{win_width}x#{win_height})"
        # Check if click is in menu bar region (not resizing)
        elsif target_name != "system_gui" && relative_y < 11
          # Start drag
          @dragging = true
          @drag_target_pid = target_pid
          @drag_offset_x = x - win_x
          @drag_offset_y = y - win_y
          puts "[KERNEL] Start drag: PID #{target_pid}, offset=(#{@drag_offset_x},#{@drag_offset_y})"
        end

        # Forward event to target app
        _send_raw_message(target_pid, FmrbConst::MSG_TYPE_HID_EVENT, data_binary)

      when 3  # Mouse move
        if @resizing && @resize_target_pid
          # Calculate new window size
          new_width = @resize_start_width + (x - @resize_start_x)
          new_height = @resize_start_height + (y - @resize_start_y)

          # Update window size
          if _update_window_size(@resize_target_pid, new_width, new_height)
            # Size updated successfully
          else
            puts "[KERNEL] Failed to update window size"
            @resizing = false
          end
        elsif @dragging && @drag_target_pid
          # Calculate new window position
          new_x = x - @drag_offset_x
          new_y = y - @drag_offset_y

          # Update window position
          if _update_window_position(@drag_target_pid, new_x, new_y)
            # Position updated successfully
          else
            puts "[KERNEL] Failed to update window position"
            @dragging = false
          end
        else
          # Not dragging or resizing - forward to current HID target app if exists
          update_window_list
          target_window = find_window_at(x, y)
          if target_window
            target_pid = target_window[:pid]
            _send_raw_message(target_pid, FmrbConst::MSG_TYPE_HID_EVENT, data_binary)
          end
        end

      when 5  # Mouse button up
        if @resizing
          puts "[KERNEL] End resize: PID #{@resize_target_pid}"
          @resizing = false
          @resize_target_pid = nil
          @resize_start_width = 0
          @resize_start_height = 0
          @resize_start_x = 0
          @resize_start_y = 0
        elsif @dragging
          puts "[KERNEL] End drag: PID #{@drag_target_pid}"
          @dragging = false
          @drag_target_pid = nil
          @drag_offset_x = 0
          @drag_offset_y = 0
        end

        # Forward event to target app if HID target is set
        update_window_list
        target_window = find_window_at(x, y)
        if target_window
          target_pid = target_window[:pid]
          _send_raw_message(target_pid, FmrbConst::MSG_TYPE_HID_EVENT, data_binary)
        end
      end

    rescue => e
      puts "[KERNEL] Error in handle_hid_event: #{e.class}: #{e.message}"
    end
  end

  def update_window_list(show_log = false)
    @window_list = _get_window_list
    if show_log
      puts "[KERNEL] Window list (#{@window_list.size}):"
      @window_list.each do |w|
        puts "[KERNEL]   PID #{w[:pid]} '#{w[:app_name]}' pos=(#{w[:x]},#{w[:y]}) size=#{w[:width]}x#{w[:height]} Z=#{w[:z_order]}"
      end
    end
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
    puts "[KERNEL] main_loop started"
    loop do
      tick_process
      _spin(@tick)
    end
  end

  def initial_sequence
    # Check protocol version with host
    puts "[KERNEL] Checking protocol version..."
    unless check_protocol_version(5000)
      puts "[KERNEL] ERROR: Protocol version check failed"
      raise "Protocol version mismatch with host"
    end
    puts "[KERNEL] Protocol version check passed"

    # Spawn system GUI app
    _spawn_app_req("system/gui_app")
    _set_hid_target(2)
  end

  def start
    initial_sequence
    main_loop
  end
end

