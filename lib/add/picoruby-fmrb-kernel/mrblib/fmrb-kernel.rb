# Family mruby OS - Kernel Framework
# Kernel provides system-level services for the Family mruby OS
class AppData
  def initialize()
    @name
    @path
    @pos_x = 0
    @pos_y = 0
    @window_width = 0
    @window_height = 0
    @visible = true
  end
end

class FmrbKernel
  def initialize()
    puts "[KERNEL] initialize"
    @app_list = []
    @window_order = []
    _init
    puts "[KERNEL] Tick = #{@tick}"
    puts "[KERNEL] Max App Number = #{@max_app_num}"
    _set_ready
  end

  def msg_handler(msg) # called from _spin
    puts "[KERNEL] Received message: type=#{msg[:type]}, src_pid=#{msg[:src_pid]}, data_size=#{msg[:data].length}"

    case msg[:type]
    when FmrbKernel::MSG_TYPE_APP_CONTROL
      handle_app_control(msg)
    when FmrbKernel::MSG_TYPE_APP_GFX
      puts "[KERNEL] Graphics message (not implemented)"
    when FmrbKernel::MSG_TYPE_APP_AUDIO
      puts "[KERNEL] Audio message (not implemented)"
    else
      puts "[KERNEL] Unknown message type: #{msg[:type]}"
    end
  end

  def handle_app_control(msg)
    data = msg[:data]
    subtype = data.bytes[0]

    case subtype
    when 1  # FMRB_APP_CTRL_SPAWN
      app_name = data[1, 32].delete("\x00")
      pid = msg[:src_pid]
      puts "[KERNEL] Spawn request from pid=#{pid}: #{app_name}"

      result = _spawn_app_req(app_name)
      if result
        puts "[KERNEL] App #{app_name} spawned successfully"

        # Set HID target to the newly spawned app
        Kernel.set_hid_target(pid)
        puts "[KERNEL] HID target set to app pid=#{pid}"

        # TODO: Add to @app_list with window info
      else
        puts "[KERNEL] Failed to spawn app: #{app_name}"
      end
    when 2  # FMRB_APP_CTRL_KILL
      pid = msg[:src_pid]
      puts "[KERNEL] Kill request from pid=#{pid} (not implemented)"
      # TODO: Reset HID target if this was the target app
      # Kernel.set_hid_target(0xFF)
    when 3  # FMRB_APP_CTRL_SUSPEND
      puts "[KERNEL] Suspend request (not implemented)"
    when 4  # FMRB_APP_CTRL_RESUME
      puts "[KERNEL] Resume request (not implemented)"
    else
      puts "[KERNEL] Unknown app control subtype: #{subtype}"
    end
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
  end

  def start
    initial_sequence
    main_loop
  end
end

