# System Desktop Application
# Background layer: wallpaper, toolbar, memory stats (z=0)

class SystemDesktopApp < FmrbApp
  def initialize
    super()
    @counter = 0
    @bg_col = 0xF6

    @st = 0
    @boot_wait = 0
    @mem_update_interval = 30  # Update memory stats every 30 frames
  end

  BOOT_DISPLAY_FRAMES = 6  # Number of frames to show boot image (~2s at 330ms/frame)

  def show_boot_image
    @gfx.clear(0x00)
    begin
      #@gfx.load_image("/boot/boot.png", coord: [0, 0])
      @gfx.load_image("/boot/boot.png", coord: :center)
      @boot_wait = BOOT_DISPLAY_FRAMES
    rescue => e
      Log.error("Boot image not available: #{e.message}")
      @boot_wait = 0
    end
  end

  def on_create()
    Log.info("on_create called")
    #show_boot_image

    # Start background music
    @audio = FmrbAudio.new(self)
    @audio.play("/data/test.nsf")

    if @boot_wait == 0
      draw_current
    end
  end

  def spawn_app(app_name)
    Log.info("Requesting spawn: #{app_name}")

    data = {
      "cmd" => "spawn",
      "app_name" => app_name
    }

    success = send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, data)

    if success
      Log.info("Spawn request sent successfully")
    else
      Log.error("Failed to send spawn request")
    end
  end

  def draw_current()
    @gfx.clear(@bg_col)
    draw_system_frame
    @gfx.present
  end

  def draw_system_frame()
    @gfx.fill_rect( 0,  0, @window_width, 13, 0xC5)
    @gfx.draw_text( 2,  2, "Family mruby", FmrbGfx::WHITE)

    # Shell button on left side of top bar
    button_x = 100
    button_y = 1
    button_width = 50
    button_height = 10

    # Button background
    @gfx.fill_rect(button_x, button_y, button_width, button_height, 0x80)
    # Button border
    @gfx.draw_rect(button_x, button_y, button_width, button_height, FmrbGfx::WHITE)
    # Button text
    @gfx.draw_text(button_x + 10, button_y + 1, "Shell", FmrbGfx::WHITE)

    @gfx.draw_line(0,13,@window_width,13,0x60)
  end

  def draw_memory_stats()
    # Update memory stats every N frames
    if @counter % @mem_update_interval == 0
      begin
        processes = FmrbApp.ps
        heap_info = FmrbApp.heap_info
        sys_pool_info = FmrbApp.sys_pool_info
        return if processes.nil?

        # Clear memory stats area at bottom-left (overwrite with background color)
        stats_area_width = 150
        stats_area_height = 65
        y_offset = @window_height - 50
        x_offset = 2
        line_height = 8

        @gfx.fill_rect(x_offset, @window_height/2, x_offset + stats_area_width, y_offset + line_height, @bg_col)

        # Draw ESP32 heap info first
        if heap_info && heap_info[:total] > 0
          heap_used_kb = (heap_info[:total] - heap_info[:free]) / 1024
          heap_total_kb = heap_info[:total] / 1024
          text = "Heap: #{heap_used_kb}KB/#{heap_total_kb}KB"
          @gfx.draw_text(x_offset, y_offset, text, FmrbGfx::BLUE)
          y_offset -= line_height
        end

        # Draw system pool info
        if sys_pool_info && sys_pool_info[:total] > 0
          sys_used_kb = sys_pool_info[:used] / 1024
          sys_total_kb = sys_pool_info[:total] / 1024
          text = "SysPool: #{sys_used_kb}KB/#{sys_total_kb}KB"
          @gfx.draw_text(x_offset, y_offset, text, FmrbGfx::BLUE)
          y_offset -= line_height
        end

        # Filter running/suspended processes
        active_procs = processes.select { |p|
          p[:state] == FmrbConst::PROC_STATE_RUNNING ||
          p[:state] == FmrbConst::PROC_STATE_SUSPENDED
        }

        active_procs.each do |proc|
          name = proc[:name]
          mem_used_kb = proc[:mem_used] / 1024
          mem_total_kb = proc[:mem_total] / 1024

          # Draw memory info: "name: XXXkB/YYYkB"
          text = "#{name}: #{mem_used_kb}KB/#{mem_total_kb}KB"
          @gfx.draw_text(x_offset, y_offset, text, FmrbGfx::BLUE)

          y_offset -= line_height
        end
      rescue => e
        Log.error("Error getting memory stats: #{e.message}")
      end
    end
  end

  def on_update()
    # Boot image display countdown
    if @boot_wait > 0
      @boot_wait -= 1
      if @boot_wait == 0
        draw_current
      end
      return 330
    end

    #debug
    if @counter == 20
      #spawn_app("default/shell")
    end

    draw_system_frame
    draw_memory_stats
    @gfx.present

    @counter += 1
    330
  end

  def on_event(ev)
    #Log.debug("on_event: gui app")
    #p ev

    # Call parent class handler first (for close button, etc.)
    super(ev)

    # Handle mouse up event
    if ev[:type] == :mouse_up
      Log.debug("Mouse button #{ev[:button]} released at (#{ev[:x]}, #{ev[:y]})")

      # Shell button hit test
      button_x = 100
      button_y = 0
      button_width = 50
      button_height = 10

      if ev[:x] >= button_x && ev[:x] < button_x + button_width &&
         ev[:y] >= button_y && ev[:y] < button_y + button_height
        Log.info("Shell button clicked")
        spawn_app("default/shell")
        return
      end
    end
  end

  def on_destroy
    Log.info("Destroyed")
  end

end

# Create and start the system GUI app instance
Log.info("SystemDesktopApp.new")
begin
  app = SystemDesktopApp.new
  Log.info("SystemDesktopApp created successfully")
  app.start
rescue => e
  Log.error("Exception caught: #{e.class}")
  Log.error("Message: #{e.message}")
  Log.error("Backtrace:")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Script ended")
