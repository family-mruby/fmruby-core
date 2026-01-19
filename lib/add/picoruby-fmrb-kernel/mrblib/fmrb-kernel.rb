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
    Log.info("initialize")
    @app_list = []
    @window_order = []
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

      result = _spawn_app_req(app_name)
      if result
        Log.info("App #{app_name} spawned successfully")

        # Set HID target to the newly spawned app
        Kernel.set_hid_target(pid)
        Log.info("HID target set to app pid=#{pid}")

        # TODO: Add to @app_list with window info
      else
        Log.error("Failed to spawn app: #{app_name}")
      end
    when "kill"
      Log.info("Kill request from pid=#{pid} (not implemented)")
      # TODO: Reset HID target if this was the target app
      # Kernel.set_hid_target(0xFF)
    when "suspend"
      Log.info("Suspend request (not implemented)")
    when "resume"
      Log.info("Resume request (not implemented)")
    else
      Log.warn("Unknown app control command: #{cmd}")
    end
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
    _spawn_app_req("system/gui_app")
    Kernel.set_hid_target(2)
  end

  def start
    initial_sequence
    main_loop
  end
end

