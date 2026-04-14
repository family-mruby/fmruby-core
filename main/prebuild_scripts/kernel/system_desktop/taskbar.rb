# Taskbar mixin for SystemDesktopApp
# Shows running app icons on the menu bar. Clicking an icon switches focus.

module TaskbarMixin
  TASKBAR_ICON_SIZE = 9   # px, square icon on 13px menu bar
  TASKBAR_ICON_PAD = 3    # px between icons
  TASKBAR_X_START = 82    # after "Family mruby" text
  TASKBAR_Y = 2           # top margin

  # Colors for app icons by VM type (index matches vm_type from FmrbApp.ps)
  TASKBAR_COLORS = [
    0xC0,  # 0: mruby    - red
    0xFA,  # 1: lua      - yellow
    0x03,  # 2: basic    - blue
    0x1C,  # 3: native   - green
  ]
  TASKBAR_FOCUSED_BORDER = 0xFF  # white border for focused app

  def init_taskbar
    @taskbar_apps = []
    @taskbar_update_counter = 0
  end

  def update_taskbar_apps
    procs = FmrbApp.ps
    apps = []
    i = 0
    while i < procs.size
      p = procs[i]
      # state 2 = RUNNING, type 0 = kernel, exclude self by name
      if p[:state] == 2 && p[:type] != 0 && p[:name] != @name
        apps << { :pid => p[:id], :name => p[:name], :vm_type => p[:vm_type] || 0 }
      end
      i += 1
    end
    @taskbar_apps = apps
  end

  def draw_taskbar
    return if @taskbar_apps.empty?

    clock_area = 95  # reserved for clock
    max_x = @window_width - clock_area
    x = TASKBAR_X_START

    @taskbar_apps.each do |app|
      break if x + TASKBAR_ICON_SIZE > max_x

      color = TASKBAR_COLORS[app[:vm_type]] || 0x6D

      # Draw icon square
      @gfx.fill_rect(x, TASKBAR_Y, TASKBAR_ICON_SIZE, TASKBAR_ICON_SIZE, color)

      # Draw first letter of app name
      label = app[:name]
      if label
        # Strip prefix (e.g. "default/" -> take part after /)
        slash = label.rindex("/")
        label = label[(slash + 1)..-1] if slash
        ch = label[0]
        @gfx.draw_text(x + 2, TASKBAR_Y + 1, ch, FmrbGfx::WHITE, color) if ch
      end

      # Focused highlight: white border
      if app[:pid] == @taskbar_focused_pid
        @gfx.draw_rect(x - 1, TASKBAR_Y - 1, TASKBAR_ICON_SIZE + 2, TASKBAR_ICON_SIZE + 2, TASKBAR_FOCUSED_BORDER)
      end

      x += TASKBAR_ICON_SIZE + TASKBAR_ICON_PAD
    end
  end

  def handle_taskbar_click(mx, my)
    return false if @taskbar_apps.empty?
    return false if my < TASKBAR_Y || my >= TASKBAR_Y + TASKBAR_ICON_SIZE

    clock_area = 95
    max_x = @window_width - clock_area
    x = TASKBAR_X_START

    @taskbar_apps.each do |app|
      break if x + TASKBAR_ICON_SIZE > max_x

      if mx >= x && mx < x + TASKBAR_ICON_SIZE
        # Request kernel to focus this app
        @taskbar_focused_pid = app[:pid]
        send_message(FmrbConst::PROC_ID_KERNEL, FmrbConst::MSG_TYPE_APP_CONTROL,
          { "cmd" => "focus_app", "pid" => app[:pid] })
        draw_foreground
        return true
      end

      x += TASKBAR_ICON_SIZE + TASKBAR_ICON_PAD
    end

    false
  end
end
