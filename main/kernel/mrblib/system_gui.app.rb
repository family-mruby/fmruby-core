# System GUI Application
# This is the default system GUI that provides basic UI elements

class SystemGuiApp < FmrbApp
  def initialize
    super()
    @counter = 0
    @bg_col = 0xF6

    @st = 0
    @mem_update_interval = 30  # Update memory stats every 30 frames
  end

  def on_create()
    puts "[SystemGUI] on_create called"
    draw_current
  end

  def spawn_app(app_name)
    puts "[SystemGUI] Requesting spawn: #{app_name}"

    # Build message payload: subtype + app_name(FMRB_MAX_PATH_LEN)
    # Convert APP_CTRL_SPAWN constant to byte string (chr is available in mruby)
    data = FmrbConst::APP_CTRL_SPAWN.chr
    data += app_name.ljust(FmrbConst::MAX_PATH_LEN, "\x00")

    # Send to Kernel using constants
    success = send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL, data)

    if success
      puts "[SystemGUI] Spawn request sent successfully"
    else
      puts "[SystemGUI] Failed to send spawn request"
    end
  end

  def draw_current()
    @gfx.clear(@bg_col)
    draw_system_frame
    @gfx.present
  end

  def draw_system_frame()
    @gfx.fill_rect( 0,  0, @window_width, 10, 0xC5)
    @gfx.draw_text( 2,  1, "Family mruby", FmrbGfx::WHITE)

    # Shell button on left side of top bar
    button_x = 100
    button_y = 0
    button_width = 50
    button_height = 10

    # Button background
    @gfx.fill_rect(button_x, button_y, button_width, button_height, 0x80)
    # Button border
    @gfx.draw_rect(button_x, button_y, button_width, button_height, FmrbGfx::WHITE)
    # Button text
    @gfx.draw_text(button_x + 10, button_y + 1, "Shell", FmrbGfx::WHITE)
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
        @gfx.fill_rect(2, @window_height - stats_area_height - 2, stats_area_width, stats_area_height, @bg_col)

        y_offset = @window_height - 10
        line_height = 8
        x_offset = 2

        # Draw ESP32 heap info first
        if heap_info && heap_info[:total] > 0
          heap_free_kb = heap_info[:free] / 1024
          heap_total_kb = heap_info[:total] / 1024
          text = "Heap: #{heap_free_kb}KB/#{heap_total_kb}KB"
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
        puts "[SystemGUI] Error getting memory stats: #{e.message}"
      end
    end
  end

  def on_update()
    if @counter % 30 == 0
      puts "[SystemGUI] on_update() tick #{@counter / 30}s"
    end

    @counter += 1

    draw_system_frame
    draw_memory_stats
    @gfx.present

    33
  end

  def on_event(ev)
    #puts "on_event: gui app"
    #p ev

    # Handle mouse up event
    if ev[:type] == :mouse_up
      puts "[SystemGUI] Mouse button #{ev[:button]} released at (#{ev[:x]}, #{ev[:y]})"

      # Shell button hit test
      button_x = 100
      button_y = 0
      button_width = 50
      button_height = 10

      if ev[:x] >= button_x && ev[:x] < button_x + button_width &&
         ev[:y] >= button_y && ev[:y] < button_y + button_height
        puts "[SystemGUI] Shell button clicked"
        spawn_app("default/shell")
        return
      end
    end
  end

  def on_destroy
    puts "[SystemGUI] Destroyed"
  end

end

# Create and start the system GUI app instance
puts "[SystemGUI] SystemGuiApp.new"
begin
  app = SystemGuiApp.new
  puts "[SystemGUI] SystemGuiApp created successfully"
  app.start
rescue => e
  puts "[SystemGUI] Exception caught: #{e.class}"
  puts "[SystemGUI] Message: #{e.message}"
  puts "[SystemGUI] Backtrace:"
  puts e.backtrace.join("\n") if e.backtrace
end
puts "[SystemGUI] Script ended"
