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
    _init
    puts "[KERNEL] Tick = #{@tick}"
    puts "[KERNEL] Max App Number = #{@max_app_num}"
    _set_ready
  end

  def msg_handler(msg) # called from _spin
    puts "[KERNEL] Received message: type=#{msg[:type]}, src_pid=#{msg[:src_pid]}, data_size=#{msg[:data].length}"

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
    x = (data_binary.getbyte(2) << 8) | data_binary.getbyte(3)
    y = (data_binary.getbyte(4) << 8) | data_binary.getbyte(5)

    puts "[KERNEL] HID event from #{src_pid}: subtype=#{subtype}, button=#{button}, pos=(#{x},#{y})"

    # Only handle mouse button events (subtype 3=down, 4=up)
    if subtype == 3 || subtype == 4
      # Update window list to get current state
      update_window_list

      # Find window at click position
      target_pid = find_window_at(x, y)

      if target_pid.nil?
        puts "[KERNEL] No window at (#{x},#{y})"
        return
      end

      puts "[KERNEL] Window hit test: (#{x},#{y}) -> PID #{target_pid}"

      # Update HID routing to target window
      _set_hid_target(target_pid)

      # Forward HID event to target app
      FmrbApp.send_message(target_pid, FmrbConst::MSG_TYPE_HID_EVENT, data_binary)
    end
  end

  def update_window_list
    @window_list = _get_window_list
    puts "[KERNEL] Window list updated: #{@window_list.size} windows"
  end

  def find_window_at(x, y)
    # Search from front to back (highest z_order first)
    # Sort by z_order descending
    sorted_windows = @window_list.sort_by { |w| -w[:z_order] }

    sorted_windows.each do |win|
      if x >= win[:x] && x < win[:x] + win[:width] &&
         y >= win[:y] && y < win[:y] + win[:height]
        return win[:pid]
      end
    end

    nil  # No window at this position
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

