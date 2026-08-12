# On-device debugger UI for the editor (design doc sec 4.5).
#
# Split out of editor.app.rb in P5. The editor body talks to it only through the
# dbg_* hooks at the top of this file, and the Spinel build of the editor gets a
# no-op module of the same name instead
# (main/prebuild_scripts/spinel/editor_debug_stub.rb) -- which is what keeps
# FMRB::Debug, its symbol-keyed Hash traffic and the pane drawing statically out
# of that build. Debugging is done in the mruby build (see plan.md stage 6).
#
# Shared editor constants (CHAR_H, BG_COLOR, SC_*, GUTTER_*, ...) come from
# EditorConst, which this module includes below; its own DBG_* constants are
# defined at the bottom.
module EditorDebugPane
  include EditorConst

  # ---- Hooks the editor body calls (the stub answers all of these too) ----

  def dbg_init
    @dbg_active = false       # session held (acquired + attached)
    @dbg_pid = nil            # attached target pid
    @dbg_stopped = false      # target currently parked
    @dbg_stop_file = nil      # file/line of the current stop
    @dbg_stop_line = nil
    @dbg_frames = []          # last stack_trace
    @dbg_vars = []            # last frame_vars for @dbg_frame_idx
    @dbg_frame_idx = 0
    @dbg_pane = :stack        # :stack or :vars
    @dbg_msg = ""             # transient status string
    # Breakpoints: { path => { line(1-based) => bp_id_or_nil } }.
    @bp = {}
    # Modal attach-target picker.
    @target_picker_open = false
    @target_list = []
    @target_idx = 0
  end

  def dbg_active?
    @dbg_active
  end

  # Height reserved at the bottom of the user area for the pane (0 = no pane).
  def dbg_pane_h
    @dbg_active ? DBG_PANE_ROWS * CHAR_H : 0
  end

  # Left gutter width for breakpoint dots (0 outside a session).
  def dbg_gutter_w
    @dbg_active ? GUTTER_W : 0
  end

  def dbg_draw_pane(y0, h)
    draw_debug_pane(y0, h)
  end

  # A modal owned by the debugger is up (attach-target picker).
  def dbg_modal?
    @target_picker_open
  end

  def dbg_draw_modal
    draw_target_picker if @target_picker_open
  end

  # Returns true when the key was consumed.
  def dbg_handle_modal_key(ev)
    return false unless @target_picker_open
    handle_target_picker_key(ev)
    true
  end

  # Debugger function keys. F9 (toggle breakpoint) works at any time; the
  # run-control keys only during a session. Returns true when consumed.
  def dbg_handle_key(ev)
    scancode = ev[:scancode] || 0
    if scancode == SC_F9
      toggle_breakpoint
      return true
    end
    return false unless @dbg_active
    case scancode
    when SC_F5  then dbg_continue
    when SC_F6  then dbg_pause
    when SC_F10 then dbg_step(:over)
    when SC_F11 then dbg_step(ev_shift?(ev) ? :out : :in)
    when SC_F7  then dbg_select_frame(-1)
    when SC_F8  then dbg_select_frame(1)
    when SC_F4  then dbg_toggle_pane
    else
      return false
    end
    true
  end

  # The menu bar shows a Debug entry only when the debugger is compiled in.
  def dbg_menu_visible?
    true
  end

  # The menu bar draws "label(D)mark"; the word is translated, the mark is not.
  def dbg_menu_label
    FmrbI18n.t(:m_debug).to_s
  end

  def dbg_menu_mark
    @dbg_active ? "*" : " "
  end

  def dbg_menu_width
    66
  end

  # Debug dropdown adapts to whether a session is active.
  def dbg_menu_items
    if @dbg_active
      ["Continue", "Step Over", "Step In", "Step Out", "Pause", "Toggle BP", "Detach"]
    else
      ["Attach...", "Toggle BP"]
    end
  end

  def dbg_activate_item(idx)
    if @dbg_active
      case idx
      when 0 then dbg_continue
      when 1 then dbg_step(:over)
      when 2 then dbg_step(:in)
      when 3 then dbg_step(:out)
      when 4 then dbg_pause
      when 5 then toggle_breakpoint
      when 6 then dbg_detach
      end
    else
      case idx
      when 0 then open_target_picker
      when 1 then toggle_breakpoint
      end
    end
  end

  # Row tint for the current stop line (BG_COLOR = no tint).
  def dbg_line_background(line_idx)
    (@dbg_stopped && stop_on_line?(line_idx)) ? STOP_BG : BG_COLOR
  end

  def dbg_draw_gutter(line_idx, y)
    draw_gutter_marker(line_idx, y)
  end

  def dbg_poll
    poll_debug_events if @dbg_active
  end

  def dbg_shutdown
    end_debug_session(true) if @dbg_active
  end

  # ---- Implementation ----

  # Bottom split pane shown only during a debug session; edit area shrinks.
  DBG_PANE_ROWS = 8
  DBG_PANE_BG   = FmrbGfx.rgb_to_332(30, 30, 45)
  DBG_HDR_BG    = FmrbGfx.rgb_to_332(60, 60, 90)
  DBG_TEXT      = FmrbGfx.rgb_to_332(220, 220, 220)
  DBG_HDR_TEXT  = FmrbGfx.rgb_to_332(255, 255, 120)
  DBG_SEL_BG    = FmrbGfx.rgb_to_332(80, 80, 130)
  # holds a red dot per breakpoint line (VSCode style); the current-stop line
  # keeps a full-row highlight.
  BP_MARK   = FmrbGfx.rgb_to_332(220, 0, 0)     # red breakpoint dot
  STOP_BG   = FmrbGfx.rgb_to_332(250, 240, 140) # current-line row highlight
  STOP_MARK = FmrbGfx.rgb_to_332(230, 160, 0)   # current-line gutter marker
  # ps filter values (fmrb_app.h): general mruby apps that are running.
  APP_TYPE_USER  = 2   # APP_TYPE_USER_APP
  VM_TYPE_MRUBY  = 0   # FMRB_VM_TYPE_MRUBY
  PROC_RUNNING   = 2   # PROC_STATE_RUNNING
  # Debug function-key scancodes (USB HID Usage IDs).

  def base(p)
    p ? p.split("/").last : ""
  end

  # Only the current-stop line gets a row highlight; breakpoints are shown by
  # the red gutter dot (draw_gutter_marker), not a row tint.
  def stop_on_line?(line_idx)
    return false unless @dbg_stopped && @dbg_stop_file && @dbg_stop_line && @current_file
    base(@dbg_stop_file) == base(@current_file) && @dbg_stop_line == line_idx + 1
  end

  def bp_on_line?(line_idx)
    m = @current_file && @bp[@current_file]
    m && m.has_key?(line_idx + 1)
  end

  # Gutter dot: red circle for a breakpoint line; a yellow ring/dot marks the
  # current-stop line (ring over the red dot when a breakpoint sits there).
  def draw_gutter_marker(line_idx, y)
    cx = @user_area_x0 + @gutter_w / 2
    cyc = y + CHAR_H / 2
    bp = bp_on_line?(line_idx)
    stop = @dbg_stopped && stop_on_line?(line_idx)
    if bp
      @gfx.fill_circle(cx, cyc, 3, BP_MARK)
      @gfx.draw_circle(cx, cyc, 3, STOP_MARK) if stop
    elsif stop
      @gfx.fill_circle(cx, cyc, 2, STOP_MARK)
    end
  end

  def draw_debug_pane(y0, pane_h)
    @gfx.fill_rect(@user_area_x0, y0, @user_area_width, pane_h, DBG_PANE_BG)
    # Header row.
    @gfx.fill_rect(@user_area_x0, y0, @user_area_width, CHAR_H, DBG_HDR_BG)
    state = @dbg_stopped ? "stop ln#{@dbg_stop_line}" : "run"
    view = (@dbg_pane == :stack) ? "Stack" : "Vars f#{@dbg_frame_idx}"
    hdr = " #{view} pid=#{@dbg_pid} #{state}"
    hdr += " #{@dbg_msg}" if @dbg_msg.length > 0
    @gfx.draw_text(@user_area_x0 + 2, y0, hdr[0, @edit_cols], DBG_HDR_TEXT, DBG_HDR_BG)

    cy = y0 + CHAR_H
    rows = DBG_PANE_ROWS - 1
    if @dbg_pane == :stack
      if @dbg_frames.empty?
        note = @dbg_stopped ? "(no frames)" : "(running - F9 set BP / F5 continue)"
        @gfx.draw_text(@user_area_x0 + 2, cy, note, DBG_TEXT, DBG_PANE_BG)
      else
        i = 0
        while i < rows && i < @dbg_frames.size
          f = @dbg_frames[i]
          txt = "##{f['idx']} #{f['func']} #{base(f['file'])}:#{f['line']}"
          bg = (i == @dbg_frame_idx) ? DBG_SEL_BG : DBG_PANE_BG
          @gfx.fill_rect(@user_area_x0, cy, @user_area_width, CHAR_H, bg) if bg != DBG_PANE_BG
          @gfx.draw_text(@user_area_x0 + 2, cy, txt[0, @edit_cols], DBG_TEXT, bg)
          cy += CHAR_H
          i += 1
        end
      end
    else
      if @dbg_vars.empty?
        note = @dbg_stopped ? "(no vars)" : "(not stopped)"
        @gfx.draw_text(@user_area_x0 + 2, cy, note, DBG_TEXT, DBG_PANE_BG)
      else
        i = 0
        while i < rows && i < @dbg_vars.size
          v = @dbg_vars[i]
          txt = "#{v['name']} = #{v['value']}"
          txt += " >" if v['ref'] && v['ref'] > 0
          @gfx.draw_text(@user_area_x0 + 2, cy, txt[0, @edit_cols], DBG_TEXT, DBG_PANE_BG)
          cy += CHAR_H
          i += 1
        end
      end
    end
  end

  # ---- Attach-target picker (modal) ----

  def open_target_picker
    # General mruby apps that are running (exclude kernel/system/self).
    @target_list = []
    FmrbApp.ps.each do |a|
      if a[:type] == APP_TYPE_USER && a[:vm_type] == VM_TYPE_MRUBY &&
         a[:state] == PROC_RUNNING && a[:name] != @name
        @target_list << a
      end
    end
    if @target_list.empty?
      @dbg_msg = "no attachable app"
      @need_redraw = true
      return
    end
    @target_idx = 0
    @target_picker_open = true
    @need_redraw = true
  end

  def draw_target_picker
    items = @target_list
    n = items.size
    w = 32 * CHAR_W
    h = (n + 2) * CHAR_H + 8
    x = @user_area_x0 + (@user_area_width - w) / 2
    y = @user_area_y0 + (@user_area_height - h) / 2
    @gfx.fill_rect(x, y, w, h, DROPDOWN_BG)
    @gfx.draw_rect(x, y, w, h, 0x60)
    @gfx.draw_text(x + 4, y + 3, "Attach to app:", DROPDOWN_TEXT, DROPDOWN_BG)
    iy = y + 3 + CHAR_H + 2
    items.each_with_index do |a, i|
      label = " #{a[:name]} (pid #{a[:id]})"
      if i == @target_idx
        @gfx.fill_rect(x + 1, iy, w - 2, CHAR_H, DROPDOWN_SEL_BG)
        @gfx.draw_text(x + 4, iy, label, DROPDOWN_SEL_TEXT, DROPDOWN_SEL_BG)
      else
        @gfx.draw_text(x + 4, iy, label, DROPDOWN_TEXT, DROPDOWN_BG)
      end
      iy += CHAR_H
    end
    @gfx.draw_text(x + 4, iy + 2, "[Enter]Attach [Esc]Cancel", DROPDOWN_TEXT, DROPDOWN_BG)
  end

  def handle_target_picker_key(ev)
    n = @target_list.size
    # Navigate by scancode (HID Usage ID); consistent on ESP32 and Linux sim.
    case ev[:scancode] || 0
    when 82  # Up
      @target_idx = (@target_idx + n - 1) % n
      @need_redraw = true
    when 81  # Down
      @target_idx = (@target_idx + 1) % n
      @need_redraw = true
    when 40, 88  # Enter
      a = @target_list[@target_idx]
      @target_picker_open = false
      dbg_attach(a[:id]) if a
    when 41  # ESC
      @target_picker_open = false
      @need_redraw = true
    end
  end

  # ---- Breakpoints ----

  def toggle_breakpoint
    unless @current_file
      @dbg_msg = "open a file first"
      @need_redraw = true
      return
    end
    line = @cy + 1
    path = @current_file
    @bp[path] ||= {}
    if @bp[path].has_key?(line)
      bp_id = @bp[path][line]
      ::FMRB::Debug.bp_clear(@dbg_pid, bp_id) if @dbg_active && @dbg_pid && bp_id
      @bp[path].delete(line)
    else
      bp_id = nil
      bp_id = ::FMRB::Debug.bp_set(@dbg_pid, path, line) if @dbg_active && @dbg_pid
      @bp[path][line] = bp_id
    end
    @need_redraw = true
  end

  # ---- Session control ----

  def dbg_attach(pid)
    unless ::FMRB::Debug.acquire
      @dbg_msg = "busy (remote in use)"
      @need_redraw = true
      return
    end
    unless ::FMRB::Debug.attach(pid)
      ::FMRB::Debug.release
      @dbg_msg = "attach failed"
      @need_redraw = true
      return
    end
    @dbg_pid = pid
    @dbg_active = true
    @dbg_stopped = false
    @dbg_frames = []
    @dbg_vars = []
    @dbg_msg = "attached #{pid}"
    # Open the target's source so breakpoints can be placed before it stops.
    # Skip if the buffer has unsaved edits (don't clobber the user's work).
    src = ::FMRB::Debug.source_file(pid)
    if src && !@modified && (@current_file.nil? || base(@current_file) != base(src))
      load_file(src)
    end
    # Arm any breakpoints already placed on the open file.
    if @current_file && @bp[@current_file]
      @bp[@current_file].keys.each do |line|
        @bp[@current_file][line] = ::FMRB::Debug.bp_set(pid, @current_file, line)
      end
    end
    recompute_layout
    ensure_cursor_visible
    @need_redraw = true
    Log.info("Editor debug: attached pid=#{pid}")
  end

  def dbg_detach
    end_debug_session(true)
    @dbg_msg = "detached"
    @need_redraw = true
  end

  def end_debug_session(do_detach)
    return unless @dbg_active
    ::FMRB::Debug.detach(@dbg_pid) if do_detach && @dbg_pid
    ::FMRB::Debug.release
    @dbg_active = false
    @dbg_stopped = false
    @dbg_pid = nil
    @dbg_frames = []
    @dbg_vars = []
    @dbg_frame_idx = 0
    # Keep breakpoint lines for redisplay but drop the (now invalid) bp_ids.
    @bp.keys.each { |path| @bp[path].keys.each { |line| @bp[path][line] = nil } }
    recompute_layout
    ensure_cursor_visible
    @need_redraw = true
  end

  def dbg_continue
    return unless @dbg_active
    ::FMRB::Debug.continue(@dbg_pid)
  end

  def dbg_pause
    return unless @dbg_active
    ::FMRB::Debug.pause(@dbg_pid)
  end

  def dbg_step(mode)
    return unless @dbg_active && @dbg_stopped
    case mode
    when :in   then ::FMRB::Debug.step_in(@dbg_pid)
    when :over then ::FMRB::Debug.step_over(@dbg_pid)
    when :out  then ::FMRB::Debug.step_out(@dbg_pid)
    end
  end

  def dbg_select_frame(delta)
    return unless @dbg_active && @dbg_stopped
    n = @dbg_frames.size
    return if n == 0
    idx = @dbg_frame_idx + delta
    idx = 0 if idx < 0
    idx = n - 1 if idx >= n
    return if idx == @dbg_frame_idx
    @dbg_frame_idx = idx
    @dbg_vars = ::FMRB::Debug.frame_vars(@dbg_pid, idx) || []
    @need_redraw = true
  end

  def dbg_toggle_pane
    @dbg_pane = (@dbg_pane == :stack) ? :vars : :stack
    @need_redraw = true
  end

  # ---- Event polling (called from on_update, non-blocking) ----

  def poll_debug_events
    return unless @dbg_active
    drained = 0
    while drained < 8
      ev = ::FMRB::Debug.poll_event(0)
      break unless ev
      handle_debug_event(ev)
      drained += 1
    end
  end

  def handle_debug_event(ev)
    return unless ev[:pid] == @dbg_pid
    case ev[:type]
    when :stopped
      @dbg_stopped = true
      @dbg_stop_file = ev[:file]
      @dbg_stop_line = ev[:line]
      @dbg_frame_idx = 0
      focus_stop_location
      refresh_debug_data
      @need_redraw = true
    when :resumed
      @dbg_stopped = false
      @dbg_frames = []
      @dbg_vars = []
      @need_redraw = true
    when :exited
      @dbg_msg = "target exited"
      end_debug_session(false)
    end
  end

  # Fetch stack + vars while the target is parked (responds promptly).
  def refresh_debug_data
    return unless @dbg_active && @dbg_stopped
    @dbg_frames = ::FMRB::Debug.stack_trace(@dbg_pid, 16) || []
    @dbg_vars = ::FMRB::Debug.frame_vars(@dbg_pid, @dbg_frame_idx) || []
  end

  # Open the stopped file (if different) and move the cursor to the stop line.
  def focus_stop_location
    return unless @dbg_stop_file
    if @current_file.nil? || base(@current_file) != base(@dbg_stop_file)
      load_file(@dbg_stop_file)
    end
    if @dbg_stop_line && @dbg_stop_line >= 1
      @cy = @dbg_stop_line - 1
      @cy = EditorCore.line_count - 1 if @cy >= EditorCore.line_count
      @cy = 0 if @cy < 0
      clamp_cx
      ensure_cursor_visible
    end
  end
end
