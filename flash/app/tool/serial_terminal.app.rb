# Serial Terminal: open a UART, show what arrives, send what is typed
# (the class UART, sig/uart.rbs; doc/app_uart). Pick the unit, the TX and RX
# pins and the speed, then Open. Each line typed in the box at the bottom is
# sent with the chosen line ending when Enter is pressed. The choices are
# remembered in /home/serial_terminal.toml (app_settings).
#
# Reading never waits: on_update takes at most READ_MAX bytes at a time and
# the screen keeps only the last KEEP lines, so a fast sender cannot stall
# the window. Bytes that arrive faster than that fill the receive buffer and
# are dropped; the status line counts them.
class SerialTerminalApp < FmrbApp
  BAUDS = ["9600", "19200", "38400", "57600", "115200"]
  EOLS = ["LF", "CRLF", "-"]
  EOL_BYTES = ["\n", "\r\n", ""]
  READ_MAX = 1024     # 20 KB/s at one call per 50 ms: more than 115200 baud
  KEEP = 60
  LINE_H = 13

  def on_create
    @ja = FmrbApp.language == "ja"
    @gfx.set_font(:ja, 12)
    @cw = @gfx.text_width("0")
    @uart = nil
    @lines = []         # [text, color]
    @cur = ""           # the line still arriving
    @cur_cols = 0
    @carry = ""         # a UTF-8 character cut at the end of a read
    @units = []
    m = FmrbUart.unit_mask
    i = 0
    while i < 8
      @units << i if (m >> i) & 1 == 1
      i += 1
    end
    load_choices
    build_ui
    if @units.empty?
      status(t("この環境では UART を使えません", "No UART in this environment"), true)
      @ui.set_enabled(:open, false)
    else
      status(t("番号とピンを選んで Open", "Pick the unit and pins, then Open"), false)
    end
    draw_screen
  end

  def t(ja, en)
    @ja ? ja : en
  end

  # ---- The choices (remembered in app_settings) ----

  def load_choices
    conf = app_settings
    tx, rx = default_pins
    @unit = conf["unit"]
    @unit = @units.empty? ? 0 : @units[0] if @units.index(@unit).nil?
    @tx = conf["tx"] || tx
    @rx = conf["rx"] || rx
    @baud = BAUDS.index(conf["baud"].to_s) || 4
    @eol = EOLS.index(conf["eol"].to_s) || 0
  end

  # The board's Grove port when it has one (TX on its first line, as the
  # serial MIDI output), else the sim's.
  def default_pins
    if FmrbHw.const_defined?(:PIN_I2C2_SDA)
      [FmrbHw::PIN_I2C2_SDA, FmrbHw::PIN_I2C2_SCL]
    else
      [47, 48]
    end
  end

  def save_choices
    save_app_settings({ "unit" => @unit, "tx" => @tx, "rx" => @rx,
                        "baud" => BAUDS[@baud], "eol" => EOLS[@eol] })
  end

  # ---- The screen ----

  def build_ui
    w = @user_area_width
    h = @user_area_height
    names = @units.map { |u| "UART#{u}" }
    names = ["-"] if names.empty?
    top = FmrbHw.pin_count - 1
    @ui = FmrbUI.new(self)
    @ui.enum(:unit, 2, 2, 62, 14, names, index: names.index("UART#{@unit}") || 0)
    @ui.label(:tx_l, 68, 2, 16, 14, "TX")
    @ui.stepper(:tx, 84, 2, 52, 14, @tx, -1, top)
    @ui.label(:rx_l, 140, 2, 16, 14, "RX")
    @ui.stepper(:rx, 156, 2, 52, 14, @rx, -1, top)
    @ui.button(:open, w - 60, 2, 58, 14, "Open")
    @ui.enum(:baud, 2, 18, 74, 14, BAUDS, index: @baud)
    @ui.label(:eol_l, 80, 18, 40, 14, t("行末", "End"))
    @ui.enum(:eol, 120, 18, 56, 14, EOLS, index: @eol)
    @ui.text_field(:line, 2, h - 16, w - 54, 14, "", max: 120)
    @ui.button(:send, w - 50, h - 16, 48, 14, t("送る", "Send"))
    @ui.set_enabled(:send, false)
    @top = 50
    @rows = (h - 18 - @top) / LINE_H
    @cols = (w - 8) / @cw
  end

  def status(text, bad)
    @status = text
    @status_color = bad ? 0xE0 : theme_accent
    @dirty = true
  end

  def draw_screen
    w = @user_area_width
    @gfx.fill_rect(0, 34, w, @top - 34 + @rows * LINE_H + 2, theme_bg)
    @gfx.draw_text(4, 35, @gfx.fit_text(@status, w - 8, ".."), @status_color)
    @gfx.draw_line(0, @top - 2, w - 1, @top - 2, theme_border)
    show = @lines
    show = show + [[@cur, theme_fg]] if @cur_cols > 0
    i = show.size > @rows ? show.size - @rows : 0
    y = @top
    while i < show.size
      @gfx.draw_text(4, y, show[i][0], show[i][1])
      y += LINE_H
      i += 1
    end
    @gfx.present unless @ui.flush
    @dirty = false
  end

  # ---- Open and close ----

  def open_port
    @uart = UART.new(unit: @unit, txd_pin: @tx, rxd_pin: @rx,
                     baudrate: BAUDS[@baud].to_i)
    save_choices
    @dropped = 0
    status(t("開きました", "Opened") + " UART#{@unit} TX #{@tx} RX #{@rx} #{BAUDS[@baud]}", false)
    set_open(true)
  rescue ArgumentError, IOError => e
    @uart = nil
    status(e.message, true)
  end

  def close_port
    @uart.close if @uart
    @uart = nil
    status(t("閉じました", "Closed"), false)
    set_open(false)
  end

  def set_open(on)
    @ui.set_text(:open, on ? "Close" : "Open")
    @ui.set_enabled(:unit, !on)
    @ui.set_enabled(:tx, !on)
    @ui.set_enabled(:rx, !on)
    @ui.set_enabled(:send, on)
  end

  # ---- Sending ----

  def send_line
    return unless @uart
    text = @ui.field_text(:line).to_s
    @uart.write(text + EOL_BYTES[@eol])
    add_line("> " + text, theme_accent)
    @ui.set_field_text(:line, "")
  end

  # ---- Receiving ----

  def add_line(text, color)
    @lines << [text, color]
    @lines.shift while @lines.size > KEEP
    @dirty = true
  end

  def end_line
    add_line(@cur, theme_fg)
    @cur = ""
    @cur_cols = 0
  end

  # One character: wrap first when it does not fit the line.
  def put(ch, cols)
    end_line if @cur_cols + cols > @cols
    @cur << ch
    @cur_cols += cols
    @dirty = true
  end

  # The bytes as text: "\n" ends a line, "\r" is skipped, a byte that is
  # neither printable ASCII nor part of a UTF-8 character shows as ".".
  def take_in(s)
    s = @carry + s unless @carry.empty?
    @carry = ""
    n = s.bytesize
    i = 0
    while i < n
      b = s.getbyte(i)
      if b == 10
        end_line
        i += 1
      elsif b == 13
        i += 1
      elsif b >= 0x20 && b < 0x7f
        put(s.byteslice(i, 1), 1)
        i += 1
      elsif b >= 0xc2 && b <= 0xf4
        len = b >= 0xf0 ? 4 : (b >= 0xe0 ? 3 : 2)
        if i + len > n
          @carry = s.byteslice(i, n - i)
          return
        end
        if utf8_tail?(s, i + 1, len - 1)
          put(s.byteslice(i, len), 2)
          i += len
        else
          put(".", 1)
          i += 1
        end
      else
        put(".", 1)
        i += 1
      end
    end
  end

  def utf8_tail?(s, i, count)
    while count > 0
      b = s.getbyte(i)
      return false if b < 0x80 || b > 0xbf
      i += 1
      count -= 1
    end
    true
  end

  # ---- Events ----

  def on_event(ev)
    case @ui.handle(ev)
    when :open then @uart ? close_port : open_port
    when :line, :send then send_line
    when :unit then @unit = @units[@ui.value(:unit)] || @unit
    when :tx then @tx = @ui.value(:tx)
    when :rx then @rx = @ui.value(:rx)
    when :eol then @eol = @ui.value(:eol)
    when :baud
      @baud = @ui.value(:baud)
      @uart.setmode(baudrate: BAUDS[@baud].to_i) if @uart
    when nil then return @ui.flush
    end
    draw_screen
  end

  def on_update
    if @uart
      s = @uart.readpartial(READ_MAX)
      take_in(s) if s
      d = @uart.dropped
      if d != @dropped
        @dropped = d
        status(t("あふれて捨てたバイト: ", "Dropped bytes: ") + d.to_s, true)
      end
    end
    draw_screen if @dirty
    @uart ? 50 : 200
  end
end
