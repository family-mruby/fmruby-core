# Input Router module for FmrbKernelImpl
# HID event routing and coordinate transformation

module InputRouterMixin
  # Must match the floor enforced by fmrb_app_update_window_size() in
  # main/app/fmrb_app.c; bare const inside a mixin method only resolves
  # against the mixin's own scope, so the constants must live here.
  MIN_WINDOW_WIDTH = 64
  MIN_WINDOW_HEIGHT = 64

  def handle_hid_event(msg)
    data_binary = msg[:data]
    src_pid = msg[:src_pid]

    # Parse HID event data (binary format)
    # Format: subtype(1 byte) + button(1 byte) + x(2 bytes) + y(2 bytes)
    return if data_binary.size < 6

    subtype = data_binary.getbyte(0)
    button = data_binary.getbyte(1)
    # Little endian: low byte first, high byte second
    x = data_binary.getbyte(2) | (data_binary.getbyte(3) << 8)
    y = data_binary.getbyte(4) | (data_binary.getbyte(5) << 8)

    begin
      case subtype
      when 4  # Mouse button down
        # Force refresh: spawn marks the list dirty BEFORE the new app's task
        # has reached PROC_STATE_RUNNING, so update_window_list() inside
        # enter_fullscreen captures a list that excludes the new fullscreen
        # app. Subsequent INIT->RUNNING transitions in C don't propagate up
        # as a dirty mark, so the cached list stays stale until something
        # else (drag, resize, focus_app, ...) marks it again. Refreshing on
        # every click costs one fmrb_app_get_window_list call (semaphore +
        # FMRB_MAX_APPS=7 loop) and removes the routing race.
        mark_window_list_dirty
        update_window_list(false)

        # In fullscreen mode, route directly to fullscreen app
        # (except menu bar area which is handled by find_window_at)
        if @fullscreen_pid && y >= $menu_bar_height
          target_window = find_window_by_pid(@fullscreen_pid)
        else
          target_window = find_window_at(x, y)
        end

        if target_window.nil?
          # Fallback to desktop (only if not suspended)
          if @desktop_pid && !app_suspended?(@desktop_pid)
            target_window = find_window_by_pid(@desktop_pid)
          end
          if target_window.nil?
            Log.info("No window at (#{x},#{y})")
            return
          end
        end

        target_pid = target_window[:pid]
        target_name = target_window[:app_name]
        target_z = target_window[:z_order]
        win_x = target_window[:x]
        win_y = target_window[:y]
        win_width = target_window[:width]
        win_height = target_window[:height]

        #Log.debug("Click at (#{x},#{y}) -> '#{target_name}' (PID #{target_pid}, Z=#{target_z})")

        # Bring clicked window to front
        _bring_to_front(target_pid)
        _set_hid_target(target_pid)
        @hid_target_pid = target_pid  # Track HID target
        mark_window_list_dirty  # Z-order changed

        # Calculate relative position within window
        relative_x = x - win_x
        relative_y = y - win_y

        #Log.debug("Relative pos in window: (#{relative_x},#{relative_y}), size=#{win_width}x#{win_height}")

        # Record mouse_down window for button_up event routing
        @mouse_down_pid = target_pid

        # Check for resize handle (bottom-right 10x10 area) first
        # Only for resizable windows
        if target_window[:resizable] &&
           relative_x >= win_width - 10 && relative_y >= win_height - 10
          # Start resize and capture mouse
          @capture_mode = :resize
          @capture_pid = target_pid
          @resize_start_width = win_width
          @resize_start_height = win_height
          @resize_start_x = x
          @resize_start_y = y
          Log.info("Start resize: PID #{target_pid}, size=(#{win_width}x#{win_height})")
        # Check if click is in menu bar region (not resizing and not close button)
        elsif target_name != "system_desktop" && target_name != "system_overlay" && relative_y < 11 && relative_x < win_width - 10
          # Start drag and capture mouse (excluding close button area on the right)
          @capture_mode = :drag
          @capture_pid = target_pid
          @drag_offset_x = x - win_x
          @drag_offset_y = y - win_y
          Log.info("Start drag: PID #{target_pid}, offset=(#{@drag_offset_x},#{@drag_offset_y})")
        end

        # Create new binary message with relative coordinates
        # Format: subtype(1 byte) + button(1 byte) + x(2 bytes) + y(2 bytes)
        relative_data = "\x00\x00\x00\x00\x00\x00"
        relative_data.setbyte(0, subtype)
        relative_data.setbyte(1, button)
        relative_data.setbyte(2, relative_x & 0xFF)        # x low byte
        relative_data.setbyte(3, (relative_x >> 8) & 0xFF) # x high byte
        relative_data.setbyte(4, relative_y & 0xFF)        # y low byte
        relative_data.setbyte(5, (relative_y >> 8) & 0xFF) # y high byte

        # Forward event with relative coordinates to the clicked window
        _send_raw_message(target_pid, FmrbConst::MSG_TYPE_HID_EVENT, relative_data)

      when 3  # Mouse move
        # Handle drag/resize operations based on @capture_mode
        if @capture_mode == :resize && @capture_pid
          # Calculate new window size with constraints
          new_width = @resize_start_width + (x - @resize_start_x)
          new_height = @resize_start_height + (y - @resize_start_y)

          # Apply minimum size constraints
          new_width = MIN_WINDOW_WIDTH if new_width < MIN_WINDOW_WIDTH
          new_height = MIN_WINDOW_HEIGHT if new_height < MIN_WINDOW_HEIGHT

          # Update window size
          if _update_window_size(@capture_pid, new_width, new_height)
            mark_window_list_dirty  # Size changed
          else
            Log.info("Failed to update window size")
            # Release capture on error
            @capture_pid = nil
            @capture_mode = nil
          end

        elsif @capture_mode == :drag && @capture_pid
          # Calculate new window position
          new_x = x - @drag_offset_x
          new_y = y - @drag_offset_y

          # Clamp to the range accepted by the C API (0..65535). The C function
          # raises ArgumentError on out-of-range values, and accumulating those
          # exceptions from mouse_move events has been observed to corrupt the
          # mruby-task context on ESP32. Clamp here to avoid the raise.
          new_x = 0 if new_x < 0
          new_x = 65535 if new_x > 65535
          new_y = 0 if new_y < 0
          new_y = 65535 if new_y > 65535

          # Update window position
          if _update_window_position(@capture_pid, new_x, new_y)
            mark_window_list_dirty  # Position changed
          else
            Log.info("Failed to update window position")
            # Release capture on error
            @capture_pid = nil
            @capture_mode = nil
          end
        end

        # Forward mouse_move event:
        # - If capture is active (drag/resize): send to @capture_pid
        # - Otherwise: send to @hid_target_pid (focused window)
        target_pid = @capture_pid || @hid_target_pid
        if target_pid
          # Convert to window-relative coordinates
          target_window = find_window_by_pid(target_pid)
          if target_window
            relative_x = x - target_window[:x]
            relative_y = y - target_window[:y]

            # Create new binary message with relative coordinates
            relative_data = "\x00\x00\x00\x00\x00\x00"
            relative_data.setbyte(0, subtype)
            relative_data.setbyte(1, button)
            relative_data.setbyte(2, relative_x & 0xFF)        # x low byte
            relative_data.setbyte(3, (relative_x >> 8) & 0xFF) # x high byte
            relative_data.setbyte(4, relative_y & 0xFF)        # y low byte
            relative_data.setbyte(5, (relative_y >> 8) & 0xFF) # y high byte

            _send_raw_message(target_pid, FmrbConst::MSG_TYPE_HID_EVENT, relative_data)
          end
        end

      when 5  # Mouse button up
        # Forward to captured window or mouse_down window
        target_pid = @capture_pid || @mouse_down_pid
        if target_pid
          # Convert to window-relative coordinates
          target_window = find_window_by_pid(target_pid)
          if target_window
            relative_x = x - target_window[:x]
            relative_y = y - target_window[:y]
            win_width = target_window[:width]
            target_name = target_window[:app_name]

            # Create new binary message with relative coordinates
            relative_data = "\x00\x00\x00\x00\x00\x00"
            relative_data.setbyte(0, subtype)
            relative_data.setbyte(1, button)
            relative_data.setbyte(2, relative_x & 0xFF)        # x low byte
            relative_data.setbyte(3, (relative_x >> 8) & 0xFF) # x high byte
            relative_data.setbyte(4, relative_y & 0xFF)        # y low byte
            relative_data.setbyte(5, (relative_y >> 8) & 0xFF) # y high byte

            _send_raw_message(target_pid, FmrbConst::MSG_TYPE_HID_EVENT, relative_data)

            # Close-button click: ask non-mruby apps to stop via APP_CONTROL so
            # their runtime can unwind in its own context (Lua hook / BASIC
            # loop poll) and let the task wrapper run its cleanup. Ruby apps
            # still self-close via their own on_event handler.
            if button == 1 && relative_y < 11 && relative_x >= win_width - 10 &&
               target_name != "system_desktop" && target_name != "system_overlay"
              info = _get_app_info(target_pid)
              if info && info[:vm_type] != :mruby
                Log.info("Close button on #{info[:vm_type]} app: request stop PID #{target_pid}")
                stop_data = MessagePack.pack({"cmd" => "stop"})
                _send_raw_message(target_pid, FmrbConst::MSG_TYPE_APP_CONTROL, stop_data)
              end
            end
          end
        end

        # Release capture and reset state based on @capture_mode
        if @capture_mode == :resize
          Log.info("End resize: PID #{@capture_pid}")
          @resize_start_width = 0
          @resize_start_height = 0
          @resize_start_x = 0
          @resize_start_y = 0
        elsif @capture_mode == :drag
          Log.info("End drag: PID #{@capture_pid}")
          @drag_offset_x = 0
          @drag_offset_y = 0
        end

        # Clear all mouse button state
        @capture_pid = nil
        @capture_mode = nil
        @mouse_down_pid = nil
      end

    rescue => e
      Log.error("Error in handle_hid_event: #{e.class}: #{e.message}")
    end
  end
end
