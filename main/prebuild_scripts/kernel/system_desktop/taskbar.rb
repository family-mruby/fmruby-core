# Taskbar mixin for SystemDesktopApp
# Shows running app icons on the menu bar. Clicking an icon switches focus.

module TaskbarMixin
  TASKBAR_ICON_SIZE = 9   # px, square icon on 13px menu bar
  TASKBAR_ICON_PAD = 3    # px between icons
  TASKBAR_X_START = 82    # after "Family mruby" text
  TASKBAR_Y = 1           # top margin; keeps the focused app's white frame
                          # clear of the menu bar's bottom border line

  # Colors for app icons by VM type (index matches vm_type from FmrbApp.ps)
  TASKBAR_COLORS = [
    0xCE,  # 0: mruby    - red
    0x27,  # 1: lua      - blue
    0x2C,  # 2: basic    - green
    0x92,  # 3: native   - gray
    0xF4,  # 4: micropython - yellow
  ]
  TASKBAR_FOCUSED_BORDER = 0xFF  # white border for focused app

  def init_taskbar
    @taskbar_apps = []
    @taskbar_update_counter = 0
    @taskbar_gen = -1
  end

  # Returns true when the app list was rebuilt (callers use this to decide
  # between a full foreground repaint and the cheap clock-only path).
  def update_taskbar_apps
    # Generation gate: FmrbApp.ps allocates a large Hash per process, and
    # for minutes at a time nothing spawns, exits or changes state. ps_gen
    # is a bare counter read; only a change pays for the real rebuild.
    gen = FmrbApp.ps_gen
    return false if gen == @taskbar_gen
    @taskbar_gen = gen

    procs = FmrbApp.ps
    apps = []
    i = 0
    while i < procs.size
      p = procs[i]
      # state 1 = INIT, 2 = RUNNING, 3 = SUSPENDED (a fullscreen app parked by
      # Ctrl+Tab is suspended but must stay clickable in the taskbar).
      # INIT counts: the kernel announces a spawn while the new task is still
      # starting, so leaving it out meant the icon waited for the next poll
      # even though the app's window was already on screen.
      # type 0 = kernel, exclude self by name.
      if (p[:state] >= 1 && p[:state] <= 3) && p[:type] != 0 && p[:name] != @name
        # Icon letter precomputed here so the 1Hz draw_taskbar allocates
        # nothing (rindex/slice/[0] all make fresh Strings).
        label = p[:name]
        if label
          slash = label.rindex("/")
          label = label[(slash + 1)..-1] if slash
        end
        apps << { :pid => p[:id], :name => p[:name], :vm_type => p[:vm_type] || 0,
                  :ch => label ? label[0] : nil }
      end
      i += 1
    end
    @taskbar_apps = apps
    true
  end

  def draw_taskbar
    return if @taskbar_apps.empty?

    # Reserved for the status cells: clock, wifi, BLE, kana and the free-IRAM
    # readout (leftmost cell starts at width-166; keep a small margin).
    clock_area = 170
    max_x = @window_width - clock_area
    x = TASKBAR_X_START

    @taskbar_apps.each do |app|
      break if x + TASKBAR_ICON_SIZE > max_x

      color = TASKBAR_COLORS[app[:vm_type]] || 0x6D

      # Draw icon square
      @gfx.fill_rect(x, TASKBAR_Y, TASKBAR_ICON_SIZE, TASKBAR_ICON_SIZE, color)

      # First letter of the app name, precomputed in update_taskbar_apps
      ch = app[:ch]
      @gfx.draw_text(x + 2, TASKBAR_Y + 1, ch, FmrbGfx::WHITE, color) if ch

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

    # Must match draw_taskbar: clicks past the drawn icons would otherwise
    # focus an app whose icon was cut off by the status cells.
    clock_area = 170
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
