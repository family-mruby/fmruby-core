# UART: a serial port for apps (sig/uart.rbs, doc/app_uart). Written the way
# upstream PicoRuby's picoruby-uart is (its methods, arguments and results),
# as a thin wrapper over the app boundary's FmrbUart (C, registered by the
# gem; main/app/fmrb_boundary_uart.c), which keeps the port, its pins and the
# receive ring under the firmware. Extensions of our own: close, closed?,
# dropped, and unit: as an Integer.
class UART
  PARITY_NONE = 0
  PARITY_EVEN = 1
  PARITY_ODD = 2
  FLOW_CONTROL_NONE = 0
  FLOW_CONTROL_RTS_CTS = 1

  # The longest FmrbUart.write (FMRB_UART_WRITE_MAX).
  WRITE_MAX = 4096

  # How many times this app has opened each unit (0-4, the most any chip
  # has). The firmware knows a unit by its number and its owner only, so
  # when the app opens a unit again (the firmware closes the one it had), a
  # UART object from before would reach the new port. Each object keeps the
  # count it was opened at; one that no longer matches is closed.
  GENERATIONS = [0, 0, 0, 0, 0]

  def initialize(
        unit:,
        txd_pin: -1,
        rxd_pin: -1,
        baudrate: 9600,
        data_bits: 8,
        stop_bits: 1,
        parity: PARITY_NONE,
        flow_control: FLOW_CONTROL_NONE,
        rts_pin: -1,
        cts_pin: -1,
        rx_buffer_size: nil)
    n = UART._unit_number(unit)
    UART._check_flow_control(flow_control)
    size = rx_buffer_size || 4096
    r = FmrbUart.open(n, txd_pin, rxd_pin, baudrate, data_bits, stop_bits, parity, size)
    UART._refuse(r, n, txd_pin, rxd_pin) if r != 0
    @unit_num = n
    @generation = GENERATIONS[n] + 1
    GENERATIONS[n] = @generation
    @baudrate = baudrate
    @line_ending = "\n"
    @closed = false
  end

  attr_reader :baudrate

  # :ESP32_UART2, "ESP32_UART2" or 2 -> 2.
  def self._unit_number(unit)
    return unit if unit.is_a?(Integer)
    s = unit.to_s
    if s.start_with?("ESP32_UART")
      d = s[10, s.length - 10].to_s
      if d.length > 0 && d.length < 3
        ok = true
        i = 0
        while i < d.length
          c = d.getbyte(i)
          ok = false if c < 48 || c > 57
          i += 1
        end
        return d.to_i if ok
      end
    end
    raise ArgumentError.new("UART: unknown unit #{unit}")
  end

  def self._check_flow_control(flow_control)
    return if flow_control == FLOW_CONTROL_NONE
    if flow_control == FLOW_CONTROL_RTS_CTS
      raise ArgumentError.new("UART: flow control is not supported here (FLOW_CONTROL_NONE only)")
    end
    raise ArgumentError.new("UART: invalid flow control mode")
  end

  # FmrbUart.open's answer as its exception (fmrb_app_uart.h lists them).
  def self._refuse(r, unit, tx, rx)
    code = r & 255
    which = r >> 8
    pin = which == 1 ? tx : rx
    role = which == 1 ? "TX" : "RX"
    case code
    when 1 then raise ArgumentError.new("UART: no UART#{unit} on this board")
    when 2 then raise ArgumentError.new("UART: #{role} pin #{pin} does not exist on this board")
    when 3 then raise ArgumentError.new("UART: give txd_pin, rxd_pin or both")
    when 4 then raise ArgumentError.new("UART: txd_pin and rxd_pin are the same pin")
    when 5 then raise ArgumentError.new("UART: baudrate must be from 300 to 5000000")
    when 6 then raise ArgumentError.new("UART: invalid format (data_bits 5-8, stop_bits 1 or 2, parity PARITY_*)")
    when 7 then raise ArgumentError.new("UART: rx_buffer_size must be a power of two from 16 to 16384")
    when 8 then raise IOError.new("UART: UART#{unit} is used by the system")
    when 9 then raise IOError.new("UART: UART#{unit} is in use by another app")
    when 10
      raise IOError.new(which == 0 ? "UART: UART#{unit} is used by the MIDI serial output" :
                                     "UART: #{role} pin #{pin} is used by the MIDI serial output")
    when 11 then raise IOError.new("UART: #{role} pin #{pin} is used by the system")
    when 12 then raise IOError.new("UART: #{role} pin #{pin} is input only (a chip's interrupt line)")
    when 13 then raise IOError.new("UART: #{role} pin #{pin} is in use by another app or driver")
    when 15 then raise IOError.new("UART: not available in this environment")
    end
    raise IOError.new("UART: UART#{unit} could not be opened")
  end

  def line_ending=(line_ending)
    unless line_ending == "\n" || line_ending == "\r" || line_ending == "\r\n"
      raise ArgumentError.new("UART: invalid line ending")
    end
    @line_ending = line_ending
  end

  def puts(str)
    write str
    unless str.end_with?(@line_ending)
      write @line_ending
    end
    nil
  end

  def setmode(
    baudrate:     nil,
    data_bits:    nil,
    stop_bits:    nil,
    parity:       nil,
    flow_control: nil,
    rts_pin:      nil,
    cts_pin:      nil)
    check_open
    if baudrate
      if baudrate < 300 || baudrate > 5000000
        raise ArgumentError.new("UART: baudrate must be from 300 to 5000000")
      end
      @baudrate = FmrbUart.set_baudrate(@unit_num, baudrate)
    end
    set_flow_control(flow_control || FLOW_CONTROL_NONE, rts_pin || -1, cts_pin || -1)
    set_format(data_bits || 8, stop_bits || 1, parity || PARITY_NONE)
    self
  end

  def set_flow_control(flow_control, rts_pin, cts_pin)
    UART._check_flow_control(flow_control)
    nil
  end

  def set_format(data_bits, stop_bits, parity)
    return if data_bits.nil? && stop_bits.nil? && parity.nil?
    if data_bits.nil? || stop_bits.nil? || parity.nil?
      raise ArgumentError.new("UART: data_bits, stop_bits and parity must be specified together")
    end
    check_open
    if data_bits < 5 || data_bits > 8 || (stop_bits != 1 && stop_bits != 2) || parity < 0 || parity > 2
      raise ArgumentError.new("UART: invalid format (data_bits 5-8, stop_bits 1 or 2, parity PARITY_*)")
    end
    FmrbUart.set_format(@unit_num, data_bits, stop_bits, parity)
    self
  end

  # len given: exactly len bytes, or nil while fewer have arrived. No len:
  # everything received, or nil when nothing has.
  def read(len = nil)
    check_open
    avail = FmrbUart.bytes_available(@unit_num)
    return nil if avail == 0
    if len
      return nil if avail < len
      avail = len
    end
    take(avail)
  end

  def readpartial(maxlen)
    check_open
    avail = FmrbUart.bytes_available(@unit_num)
    return nil if avail == 0
    take(avail < maxlen ? avail : maxlen)
  end

  def getbyte
    check_open
    s = FmrbUart.read(@unit_num, 1)
    return nil if s.bytesize == 0
    s.getbyte(0)
  end

  def ungetbyte(byte)
    check_open
    raise IOError.new("UART: rx buffer is full") unless FmrbUart.unget(@unit_num, byte & 0xff)
    nil
  end

  def bytes_available
    check_open
    FmrbUart.bytes_available(@unit_num)
  end

  # A line with its "\n", or nil while no "\n" has arrived.
  def gets
    check_open
    FmrbUart.read_line(@unit_num)
  end

  # Waits until every byte is handed to the driver; longer strings go in
  # pieces of WRITE_MAX.
  def write(str)
    check_open
    total = str.bytesize
    if total <= WRITE_MAX
      FmrbUart.write(@unit_num, str)
      return total
    end
    pos = 0
    while pos < total
      n = total - pos
      n = WRITE_MAX if n > WRITE_MAX
      FmrbUart.write(@unit_num, str.byteslice(pos, n))
      pos += n
    end
    total
  end

  # An Integer's low 8 bits, or a String's first character.
  def putc(ch)
    check_open
    if ch.is_a?(Integer)
      FmrbUart.write(@unit_num, (ch & 0xff).chr)
      return ch
    end
    unless ch.is_a?(String)
      raise TypeError.new("wrong argument type (expected Integer or String)")
    end
    FmrbUart.write(@unit_num, ch[0]) if ch.bytesize > 0
    ch
  end

  def flush
    check_open
    FmrbUart.flush(@unit_num)
    self
  end

  def clear_rx_buffer
    check_open
    FmrbUart.clear_rx(@unit_num)
    self
  end

  def clear_tx_buffer
    check_open
    FmrbUart.clear_tx(@unit_num)
    self
  end

  # time: a Float in seconds or an Integer in ms (as upstream); 100 ms when
  # left out.
  def break(time = nil)
    check_open
    ms = 100
    if time.is_a?(Float)
      ms = (time * 1000).to_i
    elsif time.is_a?(Integer)
      ms = time
    elsif !time.nil?
      raise TypeError.new("can't convert into time interval")
    end
    FmrbUart.send_break(@unit_num, ms)
    self
  end

  # The bytes that arrived while the receive buffer was full (dropped).
  def dropped
    check_open
    FmrbUart.dropped(@unit_num)
  end

  # An object the app has since opened the unit again over is closed
  # already; closing it leaves the new one open.
  def close
    return nil if @closed
    FmrbUart.close(@unit_num) if GENERATIONS[@unit_num] == @generation
    @closed = true
    nil
  end

  def closed?
    @closed || GENERATIONS[@unit_num] != @generation
  end

  private

  def check_open
    raise IOError.new("UART: closed") if closed?
  end

  # n bytes the ring is known to hold, in as many calls as the glue needs.
  def take(n)
    s = FmrbUart.read(@unit_num, n)
    while s.bytesize < n
      t = FmrbUart.read(@unit_num, n - s.bytesize)
      break if t.bytesize == 0
      s << t
    end
    s
  end
end
