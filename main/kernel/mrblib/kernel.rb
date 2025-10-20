# Family mruby OS - Kernel Main Loop
# This is the core of the Family mruby OS, running at 60Hz

class FmrbKernel
  def initialize
    @running = false
    @current_app = nil
    @frame_count = 0
    @last_frame_time = 0
    puts "Family mruby OS Kernel initialized"
  end

  def start
    @running = true
    # @last_frame_time = FmrbHAL.time_ms

    # # Initialize the system
    # init_system

    # Main loop
    #main_loop
  end

  def stop
    puts "Family mruby OS Kernel stopping..."
  end

  def load_app(app_instance)
  end

  def init_system
  end

  def deinit_system
  end

  def main_loop
    puts "Starting main loop (#{TARGET_FPS} FPS)..."

    while @running
    end

    puts "Main loop ended"
  end

  def process_system_events
  end
end

# Kernel entry point
puts "Family mruby OS Kernel starting..."

kernel = FmrbKernel.new
kernel.start

puts "Family mruby OS Kernel exit"
