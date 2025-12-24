class ShellApp < FmrbApp
  def initialize
    super()
    @current_line = ""
    @history = []  # Line history
    @cursor_x = 0
    @cursor_y = 0
    @char_width = 6
    @char_height = 8
    @prompt = "> "
    @need_redraw = false
  end

  def on_create()
    @gfx.clear(FmrbGfx::WHITE)
    draw_window_frame
    draw_prompt
    @gfx.present
    puts "[ShellApp] on_create called"
  end

  def on_update()
    if @need_redraw
      redraw_screen
      @need_redraw = false
    end
    33 # msec
  end

  def draw_prompt
    # Draw all history lines
    @history.each_with_index do |line, i|
      x = @user_area_x0 + 2
      y = @user_area_y0 + 2 + (i * @char_height)
      @gfx.draw_text(x, y, line, FmrbGfx::BLACK)
    end

    # Draw current input line
    x = @user_area_x0 + 2
    y = @user_area_y0 + 2 + (@history.length * @char_height)
    @gfx.draw_text(x, y, @prompt + @current_line, FmrbGfx::BLACK)
  end

  def redraw_screen
    # Clear user area
    @gfx.fill_rect(@user_area_x0, @user_area_y0,
                    @user_area_width, @user_area_height, FmrbGfx::WHITE)
    draw_window_frame
    draw_prompt
    @gfx.present
  end

  def on_event(ev)
    puts "on_event: shell app"
    p ev

    if ev[:type] == :key_down
      handle_key_input(ev)
    elsif ev[:type] == :key_up
      handle_key_up(ev)
    end
  end

  def handle_key_up(ev)
    keycode = ev[:keycode]

    # Window position control with arrow keys
    x = 0
    y = 0
    case keycode
    when 82  # up
      y = -10
    when 81  # down
      y = 10
    when 80  # left
      x = -10
    when 79  # right
      x = 10
    end

    if x != 0 || y != 0
      set_window_position(@pos_x + x, @pos_y + y)
    end
  end

  def handle_key_input(ev)
    keycode = ev[:keycode]
    puts "[Shell] keycode=#{keycode}"

    # Enter key (keycode 13 = CR)
    if keycode == 13
      handle_enter
      return
    end

    # Backspace key (keycode 8 = BS)
    if keycode == 8
      handle_backspace
      return
    end

    # Ignore arrow keys (79-82)
    if keycode >= 79 && keycode <= 82
      return
    end

    # Convert keycode to character (ASCII printable range)
    if keycode >= 32 && keycode <= 126
      char = keycode.chr
      @current_line += char
      @need_redraw = true
    end
  end

  def handle_enter
    puts "[Shell] Command: #{@current_line}"

    # Add current line to history
    @history << (@prompt + @current_line)

    # TODO: Execute command here and add output to history

    # Clear current line
    @current_line = ""

    # Check if we need to scroll
    max_lines = @user_area_height / @char_height
    if @history.length >= max_lines - 1
      # Remove oldest line to make room
      @history.shift
    end

    @need_redraw = true
  end

  def handle_backspace
    if @current_line.length > 0
      @current_line = @current_line[0...-1]
      @need_redraw = true
    end
  end

  def on_destroy
    puts "[ShellApp] Destroyed"
  end

end

# Create and start the system GUI app instance
puts "[ShellApp] ShellApp.new"
begin
  app = ShellApp.new
  puts "[ShellApp] ShellApp created successfully"
  app.start
rescue => e
  puts "[ShellApp] Exception caught: #{e.class}"
  puts "[ShellApp] Message: #{e.message}"
  puts "[ShellApp] Backtrace:"
  puts e.backtrace.join("\n") if e.backtrace
end
puts "[ShellApp] Script ended"
