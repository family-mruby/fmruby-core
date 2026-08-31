# HID Inspector
#   On-device diagnostic/configuration tool for USB mice whose report format
#   is not handled by the generic Boot-mouse path. Lets the user identify the
#   button/X/Y field layout interactively and append a matching [[mouse]] block
#   to /etc/hid_devices.toml -- all without a serial connection.
#
#   Keyboard-driven (mouse may be non-functional). States:
#     LIST    : connected devices; Up/Down select, Enter inspect, R refresh
#     INSPECT : live raw report bytes; W wizard, B back
#     WIZARD  : step-by-step field detection; SPACE capture, N/P step
#     RESULT  : generated TOML preview; S save, Esc discard
#
#   Raw reports arrive via on_control (cmd "hid_raw") once subscribed through
#   FmrbApp.hid_raw_subscribe(slot).

class InspectorApp < FmrbApp
  TOML_PATH = "/etc/hid_devices.toml"
  LH = 10          # text line height
  SAMPLE_CAP = 120 # max raw samples kept per wizard step
  UPDATE_MS = 80

  # The page and its furniture follow the system theme, so the inspector
  # restyles with everything else. The three below it stay as they are: green
  # for good, red for trouble and blue for raw bytes are what the reading
  # means, not how the window is decorated.
  COL_BG    = FmrbConst::THEME_WINDOW_BG         # page
  COL_TEXT  = FmrbConst::THEME_TEXT              # ink
  COL_DIM   = FmrbConst::THEME_BORDER            # muted text, rules
  COL_HI    = FmrbConst::THEME_DIR_COLOR         # headers (the theme's accent ink)
  COL_SEL   = FmrbConst::THEME_HIGHLIGHT         # selection bar
  COL_SEL_TEXT = FmrbConst::THEME_TEXT_LIGHT     # ink on the selection bar
  COL_OK    = FmrbGfx.rgb_to_332(20, 130, 40)    # dark green
  COL_WARN  = FmrbGfx.rgb_to_332(200, 40, 20)    # dark red
  COL_RAW   = FmrbGfx.rgb_to_332(20, 90, 160)    # dark blue (raw bytes)

  # Wizard step indices
  W_BASE = 0
  W_BTN  = 1
  W_MOVE = 2
  W_NUM  = 3

  def initialize
    super()
    @frame_ms = UPDATE_MS
  end

  def on_create
    @cx = @user_area_x0 + 3
    @cy0 = @user_area_y0 + 2
    @max_lines = (@user_area_height - 4) / LH
    @cols = (@user_area_width - 6) / 6  # chars per line (6px glyph width)
    @cols = 1 if @cols < 1

    @state = :list
    @devices = []
    @sel = 0
    @cur = nil
    @sub_slot = nil

    @last_report = []
    @report_count = 0
    @rlen_max = 0

    reset_wizard
    @result_toml = ""
    @result_warn = nil

    refresh_devices
    @dirty = true
    draw
  end

  def on_update
    draw if @dirty
    @frame_ms
  end

  def on_destroy
    do_unsubscribe
  end

  def on_suspend
    do_unsubscribe
  end

  # ---- Input ----------------------------------------------------------------

  def on_event(ev)
    if ev[:type] == :key_down
      handle_key(ev)
    else
      super(ev)
    end
  end

  # ---- Raw report intake (from USB task) ------------------------------------

  def on_control(msg)
    return unless msg["cmd"] == "hid_raw"
    return if @cur.nil? || msg["slot"] != @cur[:slot]

    data = msg["data"]
    return unless data.is_a?(Array)

    @last_report = data
    @report_count += 1
    @rlen_max = data.length if data.length > @rlen_max

    if @collecting && @samples.length < SAMPLE_CAP
      @samples.push(data)
    end
    verify_report(data) if @state == :verify
    @dirty = true
  end

  private

  # ---- Subscription ---------------------------------------------------------

  def do_subscribe(slot)
    do_unsubscribe
    FmrbApp.hid_raw_subscribe(slot)
    @sub_slot = slot
  end

  def do_unsubscribe
    if @sub_slot
      FmrbApp.hid_raw_unsubscribe(@sub_slot)
      @sub_slot = nil
    end
  end

  def refresh_devices
    @devices = FmrbApp.usb_devices || []
    @sel = 0 if @sel >= @devices.length
    @sel = 0 if @sel < 0
  end

  # ---- Key handling ---------------------------------------------------------

  def handle_key(ev)
    sc = ev[:scancode]
    ch = key_char(ev)

    case @state
    when :list
      if sc == FmrbConst::KEY_UP
        @sel -= 1; @sel = @devices.length - 1 if @sel < 0
      elsif sc == FmrbConst::KEY_DOWN
        @sel += 1; @sel = 0 if @sel >= @devices.length
      elsif sc == FmrbConst::KEY_ENTER
        enter_inspect
      elsif ch == "r"
        refresh_devices
      elsif sc == FmrbConst::KEY_ESC
        stop
        return
      end
    when :inspect
      if ch == "w"
        enter_wizard
      elsif ch == "b" || sc == FmrbConst::KEY_ESC
        leave_to_list
      end
    when :wizard
      if sc == FmrbConst::KEY_SPACE
        capture_step
      elsif ch == "n"
        next_step
      elsif ch == "p"
        prev_step
      elsif sc == FmrbConst::KEY_ESC
        @state = :inspect
        @collecting = false
      end
    when :verify
      if ch == "n" || sc == FmrbConst::KEY_ENTER
        build_result
        @state = :result
      elsif ch == "p"
        @wstep = W_MOVE
        start_collecting
        @state = :wizard
      elsif ch == "c"
        @vx = @user_area_x0 + @user_area_width / 2
        @vy = @user_area_y0 + (@user_area_height + 18) / 2
      elsif sc == FmrbConst::KEY_ESC
        @state = :inspect
      end
    when :result
      if ch == "s"
        save_toml
      elsif sc == FmrbConst::KEY_ESC
        leave_to_list
      end
    end
    @dirty = true
  end

  def key_char(ev)
    c = ev[:character] || 0
    return nil if c <= 0
    s = c.chr rescue nil
    s && s.downcase
  end

  # ---- State transitions ----------------------------------------------------

  def enter_inspect
    return if @devices.empty?
    @cur = @devices[@sel]
    @last_report = []
    @report_count = 0
    @rlen_max = @cur[:report_len] || 0
    do_subscribe(@cur[:slot])
    @state = :inspect
  end

  def leave_to_list
    do_unsubscribe
    @cur = nil
    @state = :list
    refresh_devices
  end

  def enter_wizard
    reset_wizard
    @wstep = W_BASE
    start_collecting
    @state = :wizard
  end

  def reset_wizard
    @wstep = W_BASE
    @collecting = false
    @samples = []
    @baseline = []
    @btn_field = nil
    @x_raw = nil
    @y_raw = nil
    @move_range = nil   # [lo,hi] bit range that moved during the shake step
  end

  def start_collecting
    @collecting = true
    @samples = []
  end

  # ---- Wizard step processing -----------------------------------------------

  def capture_step
    @collecting = false
    case @wstep
    when W_BASE
      @baseline = @samples.empty? ? Array.new(rlen, 0) : @samples.last
    when W_BTN
      @btn_field = detect_button
    when W_MOVE
      counts = bit_change_counts(@samples)
      mask_button_bits(counts)
      @move_range = significant_range(counts, @samples.length)
      split_move_range
    end
  end

  # Split the moved bit range in half: lower half = X, upper half = Y.
  # Standard mice place X then Y, contiguous and equal width, so halving the
  # shaken range recovers both fields (and divides a shared byte at the nibble).
  def split_move_range
    @x_raw = nil
    @y_raw = nil
    return unless @move_range
    lo = @move_range[0]
    hi = @move_range[1]
    total = hi - lo + 1
    return if total < 2
    half = total / 2
    @x_raw = [lo, lo + half - 1]
    @y_raw = [lo + half, hi]
  end

  # Zero out counts for bits inside the detected button field, so an accidental
  # click during the shake step does not pollute the moved range.
  def mask_button_bits(counts)
    return unless @btn_field
    b = @btn_field[:off]
    last = b + @btn_field[:size] - 1
    while b <= last && b < counts.length
      counts[b] = 0
      b += 1
    end
  end

  def next_step
    if @wstep < W_NUM - 1
      @wstep += 1
      start_collecting
    else
      compute_layout
      enter_verify
    end
  end

  # Live test of the detected layout: parse incoming reports with @lay_* and
  # move a dot so the user can confirm the mapping before saving.
  def enter_verify
    @collecting = false
    @vx = @user_area_x0 + @user_area_width / 2
    @vy = @user_area_y0 + (@user_area_height + 18) / 2
    @vbtn = 0
    @state = :verify
  end

  def prev_step
    if @wstep > 0
      @wstep -= 1
      start_collecting
    end
  end

  def rlen
    return @rlen_max if @rlen_max > 0
    l = @cur && @cur[:report_len] ? @cur[:report_len] : 0
    l > 0 ? l : (@last_report.length > 0 ? @last_report.length : 4)
  end

  # Return [min_bit, max_bit] of bits differing from baseline across samples.
  def changed_bit_range(samples)
    base = @baseline.empty? ? Array.new(rlen, 0) : @baseline
    minb = nil
    maxb = nil
    samples.each do |rep|
      n = rep.length < base.length ? rep.length : base.length
      i = 0
      while i < n
        diff = rep[i] ^ base[i]
        if diff != 0
          b = 0
          while b < 8
            if (diff & (1 << b)) != 0
              bit = i * 8 + b
              minb = bit if minb.nil? || bit < minb
              maxb = bit if maxb.nil? || bit > maxb
            end
            b += 1
          end
        end
        i += 1
      end
    end
    return nil if minb.nil?
    [minb, maxb]
  end

  # Per-bit count of how many samples that bit changed from baseline, summed
  # over the whole gesture (an integrated statistic: a momentary off-axis
  # spike adds at most a few counts, while sustained intended motion piles up
  # many). Per-bit resolution lets us split fields that share a byte (e.g. the
  # 12-bit packed format's middle byte = X high nibble + Y low nibble).
  def bit_change_counts(samples)
    base = @baseline.empty? ? Array.new(rlen, 0) : @baseline
    nbits = base.length * 8
    counts = Array.new(nbits, 0)
    samples.each do |rep|
      n = rep.length < base.length ? rep.length : base.length
      i = 0
      while i < n
        diff = rep[i] ^ base[i]
        if diff != 0
          b = 0
          while b < 8
            counts[i * 8 + b] += 1 if (diff & (1 << b)) != 0
            b += 1
          end
        end
        i += 1
      end
    end
    counts
  end

  # Bits active in a meaningful fraction of samples -> [min,max] (provisional).
  def significant_range(counts, n)
    return nil unless counts
    thr = n / 8
    thr = 2 if thr < 2
    minb = nil
    maxb = nil
    bit = 0
    while bit < counts.length
      if counts[bit] >= thr
        minb = bit if minb.nil?
        maxb = bit
      end
      bit += 1
    end
    return nil if minb.nil?
    [minb, maxb]
  end

  # Buttons: snap the changed range to the enclosing byte (8-bit field).
  def detect_button
    r = changed_bit_range(@samples)
    return nil unless r
    byte = r[0] / 8
    { off: byte * 8, size: 8 }
  end

  # ---- Layout synthesis + TOML ----------------------------------------------

  # Turn the detected raw bit ranges into a concrete layout (offsets are
  # relative to report data, i.e. after the Report ID byte if present).
  # Stored in @lay_* so both the VERIFY step and the TOML output use the
  # exact same interpretation.
  def compute_layout
    rl = rlen

    # Report ID heuristic: buttons not in byte0 => byte0 is a constant Report ID.
    has_id = @btn_field && @btn_field[:off] >= 8
    shift = has_id ? 8 : 0

    btn_off = (@btn_field ? @btn_field[:off] : 0) - shift
    btn_off = 0 if btn_off < 0

    # @x_raw / @y_raw were produced by halving the shaken bit range
    # (lower half = X, upper half = Y) in capture_step.
    x = field_from_raw(@x_raw, shift)
    y = field_from_raw(@y_raw, shift)

    # Snap to known templates for the common cases the generic path misses.
    if !has_id && (rl == 3) && x && y &&
       near?(x[:off], 8) && near?(y[:off], 16)
      # Standard 3-byte Boot mouse
      x = { off: 8, size: 8 }
      y = { off: 16, size: 8 }
    elsif !has_id && (rl >= 5) && x && y &&
          near?(x[:off], 8) && y && y[:off] >= 16
      # 12-bit packed (e.g. generic OEM "USB Optical Mouse")
      x = { off: 8, size: 12 }
      y = { off: 20, size: 12 }
    end

    @lay_warn = nil
    if x.nil? || y.nil?
      @lay_warn = "X/Y not detected - shake harder and retry"
      x = { off: 8, size: 8 } if x.nil?
      y = { off: 16, size: 8 } if y.nil?
    end

    @lay_has_id = has_id
    @lay_report_id = has_id ? (@baseline[0] || 0) : nil
    @lay_data_len = rl - (has_id ? 1 : 0)
    @lay_btn_off = btn_off
    @lay_x = x
    @lay_y = y
  end

  def build_result
    @result_warn = @lay_warn
    vid = @cur[:vid]
    pid = @cur[:pid]
    if toml_has_vidpid?(vid, pid)
      @result_warn = "VID/PID already in TOML - duplicate appended"
    end
    @result_toml = build_toml(vid, pid, @lay_report_id, @lay_data_len,
                              @lay_btn_off, @lay_x, @lay_y)
  end

  # Convert a detected raw bit range into {off,size} with size snapped to a
  # sane field width (8 or 12 bits), offset relative to report data.
  def field_from_raw(raw, shift)
    return nil unless raw
    off = raw[0] - shift
    off = 0 if off < 0
    span = raw[1] - raw[0] + 1
    size = span <= 8 ? 8 : (span <= 12 ? 12 : 16)
    { off: off, size: size }
  end

  def near?(a, b)
    (a - b).abs <= 2
  end

  # Extract a signed little-endian bit field from a raw report using the
  # detected layout (offset is relative to data, after Report ID if present).
  def extract_field(report, off, size)
    base = (@lay_has_id ? 8 : 0) + off
    val = 0
    i = 0
    while i < size
      bit = base + i
      byte = bit / 8
      if byte < report.length
        val |= ((report[byte] >> (bit % 8)) & 1) << i
      end
      i += 1
    end
    val -= (1 << size) if size > 0 && (val & (1 << (size - 1))) != 0  # sign-extend
    val
  end

  def verify_buttons(report)
    byte = (@lay_has_id ? 1 : 0) + @lay_btn_off / 8
    byte < report.length ? report[byte] : 0
  end

  # Apply one report to the test dot using the detected layout.
  def verify_report(report)
    return unless @lay_x && @lay_y
    @vx += extract_field(report, @lay_x[:off], @lay_x[:size])
    @vy += extract_field(report, @lay_y[:off], @lay_y[:size])
    x0 = @user_area_x0 + 3
    x1 = @user_area_x0 + @user_area_width - 3
    y0 = @user_area_y0 + 24
    y1 = @user_area_y0 + @user_area_height - 12
    @vx = x0 if @vx < x0
    @vx = x1 if @vx > x1
    @vy = y0 if @vy < y0
    @vy = y1 if @vy > y1
    @vbtn = verify_buttons(report)
  end

  def build_toml(vid, pid, report_id, report_len, btn_off, x, y)
    lines = []
    lines << "# Auto-generated by HID Inspector"
    lines << "[[mouse]]"
    lines << sprintf("vid = 0x%04X", vid)
    lines << sprintf("pid = 0x%04X", pid)
    lines << "name = \"Inspector detected mouse\""
    lines << sprintf("report_id = 0x%02X", report_id) if report_id
    lines << "report_len = #{report_len}"
    lines << "buttons = { offset = #{btn_off}, size = 8, min = 0, max = 1, relative = false }"
    lines << axis_line("x", x)
    lines << axis_line("y", y)
    lines.join("\n")
  end

  def axis_line(name, f)
    lim = (1 << (f[:size] - 1))
    mn = -(lim - 1)
    mx = lim - 1
    "#{name} = { offset = #{f[:off]}, size = #{f[:size]}, min = #{mn}, max = #{mx}, relative = true }"
  end

  def toml_has_vidpid?(vid, pid)
    content = read_toml
    return false unless content
    vh = sprintf("0x%04x", vid)
    content.downcase.include?(vh) && content.downcase.include?(sprintf("0x%04x", pid))
  end

  def read_toml
    content = nil
    begin
      f = File.open(TOML_PATH, "r")
      content = f.read
      f.close
    rescue => e
      Log.warn("Inspector: cannot read #{TOML_PATH}: #{e.message}")
    end
    content
  end

  def save_toml
    existing = read_toml || ""
    out = existing
    out << "\n" unless out.empty? || out[-1] == "\n"
    out << "\n" << @result_toml << "\n"
    begin
      f = File.open(TOML_PATH, "w")
      f.write(out)
      f.close
      @result_warn = "Saved. Reboot to apply."
      Log.info("Inspector: appended mouse config to #{TOML_PATH}")
    rescue => e
      @result_warn = "Save failed: #{e.message}"
      Log.error("Inspector: write failed: #{e.message}")
    end
  end

  # ---- Drawing --------------------------------------------------------------

  def draw
    @dirty = false
    return unless @gfx
    clear_user_area(COL_BG)
    case @state
    when :list    then draw_list
    when :inspect then draw_inspect
    when :wizard  then draw_wizard
    when :verify  then draw_verify
    when :result  then draw_result
    end
    draw_window_frame
    @gfx.present
  end

  def line(i, str, color = COL_TEXT)
    return if i >= @max_lines
    @gfx.draw_text(@cx, @cy0 + i * LH, str, color, COL_BG)
  end

  # Draw str wrapped at @cols, one display row per chunk starting at `row`.
  # Returns the next free row index.
  def draw_wrapped(row, str, color = COL_TEXT)
    if str.length <= @cols
      line(row, str, color)
      return row + 1
    end
    pos = 0
    while pos < str.length && row < @max_lines
      line(row, str[pos, @cols], color)
      pos += @cols
      row += 1
    end
    row
  end

  def draw_list
    line(0, "HID Inspector - Devices", COL_HI)
    if @devices.empty?
      line(2, "No USB devices connected.", COL_DIM)
    else
      i = 0
      while i < @devices.length
        d = @devices[i]
        label = sprintf("%s %04X:%04X len%d%s",
                        d[:type], d[:vid], d[:pid], d[:report_len] || 0,
                        d[:layout_valid] ? "" : " ?")
        row = 2 + i
        if i == @sel
          @gfx.fill_rect(@user_area_x0 + 1, @cy0 + row * LH - 1,
                         @user_area_width - 2, LH, COL_SEL)
        end
        line(row, "#{i == @sel ? '>' : ' '} #{label}",
             i == @sel ? COL_SEL_TEXT : COL_TEXT)
        i += 1
      end
    end
    line(@max_lines - 1, "Up/Dn Enter:inspect R:refresh Esc:quit", COL_DIM)
  end

  def draw_inspect
    line(0, sprintf("Inspect %04X:%04X (%s)",
                    @cur[:vid], @cur[:pid], @cur[:type]), COL_HI)
    line(1, "Reports: #{@report_count}  len: #{@last_report.length}", COL_DIM)
    line(3, "Raw:", COL_DIM)
    line(4, hex_str(@last_report), COL_RAW)
    line(6, "Move the mouse to see live bytes.", COL_DIM)
    line(@max_lines - 1, "W:wizard B:back Esc:back", COL_DIM)
  end

  def draw_wizard
    titles = ["1/3 Baseline", "2/3 Left button", "3/3 Move (X & Y)"]
    instrs = [
      "Keep mouse STILL, then SPACE.",
      "Hold LEFT button, then SPACE.",
      "Shake L/R & U/D HARD, then SPACE.",
    ]
    line(0, "Wizard " + titles[@wstep], COL_HI)
    line(1, instrs[@wstep], COL_TEXT)
    line(3, "Samples: #{@samples.length}#{@collecting ? ' (collecting)' : ''}", COL_DIM)
    line(4, "Raw: " + hex_str(@last_report), COL_RAW)

    # Show what has been detected so far
    r = 6
    line(r, "baseline: " + hex_str(@baseline), COL_DIM); r += 1
    if @btn_field
      line(r, "buttons @bit #{@btn_field[:off]}", COL_OK); r += 1
    end
    line(r, "moved bits: " + range_str(@move_range), @move_range ? COL_OK : COL_DIM); r += 1
    line(r, "  -> X: " + range_str(@x_raw) + "  Y: " + range_str(@y_raw),
         @x_raw ? COL_OK : COL_DIM); r += 1

    nxt = @wstep < W_NUM - 1 ? "N:next" : "N:verify"
    line(@max_lines - 1, "SPACE:capture #{nxt} P:prev Esc:cancel", COL_DIM)
  end

  def draw_verify
    line(0, "Verify - does the dot follow?", COL_HI)
    bl = (@vbtn & 0x01) != 0
    br = (@vbtn & 0x02) != 0
    bm = (@vbtn & 0x04) != 0
    line(1, "Btn:  L#{bl ? '*' : '-'}  R#{br ? '*' : '-'}  M#{bm ? '*' : '-'}",
         (@vbtn != 0) ? COL_OK : COL_DIM)
    lx = @lay_x || { off: 0, size: 0 }
    ly = @lay_y || { off: 0, size: 0 }
    line(2, "x@#{lx[:off]}/#{lx[:size]} y@#{ly[:off]}/#{ly[:size]}", COL_DIM)
    # The test dot, moved by verify_report using the detected layout.
    @gfx.fill_circle(@vx, @vy, 3, COL_RAW) if @gfx
    line(@max_lines - 1, "Enter:OK P:redo C:center Esc:cancel", COL_DIM)
  end

  def draw_result
    line(0, "Result - TOML preview", COL_HI)
    rows = @result_toml.split("\n")
    row = 2
    limit = @max_lines - 2  # leave last 2 rows for warning + footer
    i = 0
    while i < rows.length && row < limit
      row = draw_wrapped(row, rows[i], COL_TEXT)
      i += 1
    end
    if @result_warn
      draw_wrapped(@max_lines - 2, @result_warn, COL_WARN)
    end
    line(@max_lines - 1, "S:save to /etc Esc:discard", COL_DIM)
  end

  def range_str(r)
    return "-" unless r
    "#{r[0]}..#{r[1]}"
  end

  def hex_str(bytes)
    return "(none)" if bytes.nil? || bytes.empty?
    out = ""
    i = 0
    while i < bytes.length
      out << sprintf("%02X ", bytes[i] & 0xFF)
      i += 1
    end
    out
  end
end

Log.info("InspectorApp.new")
begin
  app = InspectorApp.new
  app.start
rescue => e
  Log.error("Inspector exception: #{e.class}: #{e.message}")
  Log.error(e.backtrace.join("\n")) if e.backtrace
end
Log.info("Inspector ended")
