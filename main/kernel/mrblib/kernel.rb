# Family mruby OS - Kernel Main Loop
# This is the core of the Family mruby OS, running at 60Hz

require 'fmrb-app'

class FmrbKernel
  TARGET_FPS = 60
  FRAME_TIME_MS = (1000.0 / TARGET_FPS).to_i

  attr_reader :running, :current_app

  def initialize
    @running = false
    @current_app = nil
    @frame_count = 0
    @last_frame_time = 0
    puts "Family mruby OS Kernel initialized"
  end

  def start
    puts "Family mruby OS Kernel starting..."
    @running = true
    @last_frame_time = FmrbHAL.time_ms

    # Initialize the system
    init_system

    # Main loop
    main_loop
  end

  def stop
    puts "Family mruby OS Kernel stopping..."
    @running = false

    # Stop current app if running
    if @current_app
      @current_app.stop
      @current_app = nil
    end

    # Cleanup system
    deinit_system
  end

  def load_app(app_instance)
    # Stop previous app
    if @current_app
      @current_app.stop
    end

    # Start new app
    @current_app = app_instance
    @current_app.start

    puts "Loaded app: #{@current_app.name}"
  end

  private

  def init_system
    puts "Initializing Family mruby OS..."

    # Initialize HAL
    ret = FmrbHAL.init
    if ret != FmrbHAL::OK
      puts "ERROR: Failed to initialize HAL: #{ret}"
      @running = false
      return
    end
    puts "HAL initialized"

    # Initialize Audio
    ret = FmrbAudio.init
    if ret != FmrbAudio::OK
      puts "WARNING: Failed to initialize Audio: #{ret}"
    else
      puts "Audio initialized"
    end

    puts "Family mruby OS initialization complete"
  end

  def deinit_system
    puts "Shutting down Family mruby OS..."

    FmrbAudio.deinit
    FmrbHAL.deinit

    puts "Family mruby OS shutdown complete"
  end

  def main_loop
    puts "Starting main loop (#{TARGET_FPS} FPS)..."

    while @running
      frame_start_time = FmrbHAL.time_ms
      delta_time = frame_start_time - @last_frame_time
      @last_frame_time = frame_start_time

      # Update frame counter
      @frame_count += 1

      # Process system events
      # TODO: Read from system message queue
      process_system_events

      # Update current app
      if @current_app && @current_app.running
        @current_app.update(delta_time)
      end

      # Frame rate limiting
      frame_end_time = FmrbHAL.time_ms
      frame_elapsed = frame_end_time - frame_start_time
      sleep_time = FRAME_TIME_MS - frame_elapsed

      if sleep_time > 0
        FmrbHAL.delay_ms(sleep_time)
      elsif frame_elapsed > FRAME_TIME_MS * 2
        # Log if we're significantly over budget
        if (@frame_count % 60) == 0
          puts "WARNING: Frame #{@frame_count} took #{frame_elapsed}ms (target: #{FRAME_TIME_MS}ms)"
        end
      end

      # Periodic status log
      if (@frame_count % (TARGET_FPS * 10)) == 0
        puts "Kernel running: Frame #{@frame_count}, App: #{@current_app ? @current_app.name : 'none'}"
      end
    end

    puts "Main loop ended"
  end

  def process_system_events
    # TODO: Implement system event processing
    # - Read HID events from system queue
    # - Dispatch to current app
    # - Handle system-level events (app switching, etc.)
  end
end

# Kernel entry point
puts "Family mruby OS Kernel starting..."

kernel = FmrbKernel.new
kernel.start
