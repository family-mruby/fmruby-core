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
    puts "[KERNEL] msg_handler START"
    puts "[KERNEL] Received message: type=#{msg[:type]}, src_pid=#{msg[:src_pid]}, data_size=#{msg[:data].length}"
    puts "[KERNEL] MSG_TYPE_HID_EVENT=#{FmrbConst::MSG_TYPE_HID_EVENT}"

    case msg[:type]
    when FmrbConst::MSG_TYPE_APP_CONTROL
      puts "[KERNEL] Calling handle_app_control..."
      handle_app_control(msg)
    when FmrbConst::MSG_TYPE_HID_EVENT
      puts "[KERNEL] Calling handle_hid_event..."
      handle_hid_event(msg)
      puts "[KERNEL] handle_hid_event returned"
    when FmrbConst::MSG_TYPE_APP_GFX
      puts "[KERNEL] Graphics message (not implemented)"
    when FmrbConst::MSG_TYPE_APP_AUDIO
      puts "[KERNEL] Audio message (not implemented)"
    else
      puts "[KERNEL] Unknown message type: #{msg[:type]}"
    end
    puts "[KERNEL] msg_handler END"
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
    puts "[KERNEL] handle_hid_event START"
    data_binary = msg[:data]
    src_pid = msg[:src_pid]
    puts "[KERNEL] data_binary.size=#{data_binary.size}"

    # Parse HID event data (binary format)
    # Format: subtype(1 byte) + button(1 byte) + x(2 bytes) + y(2 bytes)
    if data_binary.size < 6
      puts "[KERNEL] Data too small, returning"
      return
    end

    subtype = data_binary.getbyte(0)
    puts "[KERNEL] subtype=#{subtype}"
    button = data_binary.getbyte(1)
    puts "[KERNEL] button=#{button}"
    # Little endian: low byte first, high byte second
    x = data_binary.getbyte(2) | (data_binary.getbyte(3) << 8)
    puts "[KERNEL] x=#{x}"
    y = data_binary.getbyte(4) | (data_binary.getbyte(5) << 8)
    puts "[KERNEL] y=#{y}"

    puts "[KERNEL] HID event from #{src_pid}: subtype=#{subtype}, button=#{button}, pos=(#{x},#{y})"

    # Only handle mouse button events (subtype 4=down, 5=up)
    puts "[KERNEL] Checking subtype condition: subtype==4 is #{subtype == 4}, subtype==5 is #{subtype == 5}"
    if subtype == 4 || subtype == 5
      begin
        puts "[KERNEL] Processing mouse button event..."

        # Update window list to get current state
        puts "[KERNEL] Calling update_window_list..."
        update_window_list
        puts "[KERNEL] update_window_list done"

        # Find window at click position
        puts "[KERNEL] Calling find_window_at(#{x}, #{y})..."
        target_pid = find_window_at(x, y)
        puts "[KERNEL] find_window_at returned: #{target_pid.inspect}"

        if target_pid.nil?
          puts "[KERNEL] No window at (#{x},#{y})"
          return
        end

        puts "[KERNEL] Window hit test: (#{x},#{y}) -> PID #{target_pid}"

        # Update HID routing to target window
        puts "[KERNEL] Calling _set_hid_target(#{target_pid})..."
        _set_hid_target(target_pid)
        puts "[KERNEL] _set_hid_target done"

        # Forward HID event to target app (keep binary format)
        puts "[KERNEL] Calling _send_raw_message(#{target_pid}, #{FmrbConst::MSG_TYPE_HID_EVENT}, data)..."
        result = _send_raw_message(target_pid, FmrbConst::MSG_TYPE_HID_EVENT, data_binary)
        puts "[KERNEL] _send_raw_message returned: #{result.inspect}"
        puts "[KERNEL] Message send result: #{result ? "success" : "failed"}"
      rescue => e
        puts "[KERNEL] Error in handle_hid_event: #{e.class}: #{e.message}"
      end
    else
      puts "[KERNEL] Subtype #{subtype} not handled (only 4 or 5)"
    end
    puts "[KERNEL] handle_hid_event END"
  end

  def update_window_list
    @window_list = _get_window_list
    puts "[KERNEL] Window list updated: #{@window_list.size} windows"
  end

  def find_window_at(x, y)
    # Search from front to back (highest z_order first)
    # Find window with highest z_order that contains the point
    target_pid = nil
    max_z_order = -1

    @window_list.each do |win|
      if x >= win[:x] && x < win[:x] + win[:width] &&
         y >= win[:y] && y < win[:y] + win[:height]
        if win[:z_order] > max_z_order
          max_z_order = win[:z_order]
          target_pid = win[:pid]
        end
      end
    end

    target_pid
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

