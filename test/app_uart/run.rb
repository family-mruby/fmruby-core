#!/usr/bin/env ruby
# Host tests of an app's UART (doc/app_uart U1).
#
#   - C (test/app_uart/uart_test.c, compiled here with gcc): the receive ring
#     and the pin table's UART rule, once per board over the ESP32 pin manager
#     with that board's real fmrb_pin_assign.h, and once over the POSIX one
#     (the sim's stand-in for NARYA v3): the units an app may open, system
#     pins, a chip's interrupt line as TX, another app's pins.
#   - Ruby: the class UART (lib/gems/picoruby-fmrb-app/mrblib/fmrb-uart.rb)
#     over a stand-in for FmrbUart (a loopback with the C side's rules: the
#     ring and its drops, gets' line end, the answers of open), and upstream
#     picoruby-uart's test/uart_test.rb with its unit name replaced.
#
#   ruby test/app_uart/run.rb        (or: rake app_uart:test)

require "tmpdir"
require "open3"

ROOT = File.expand_path("../..", __dir__)
$failures = 0
$count = 0

def check(name, got, want)
  $count += 1
  return if got == want

  $failures += 1
  puts "FAIL #{name}: got #{got.inspect}, want #{want.inspect}"
end

def raised
  yield
  nil
rescue => e
  [e.class.name, e.message]
end

# ---- C: the ring and the pin table, per board ------------------------------

BOARDS = {
  # board => [chip define, app units, system units, tx, rx, a system pin, an interrupt line]
  "NARYAV3" => [nil, [2], [0, 1], 47, 48, 43, -1],
  "TAB5" => ["CONFIG_IDF_TARGET_ESP32P4", [1, 2, 3, 4], [0], 53, 54, 31, -1],
  "NARYAV4" => ["CONFIG_IDF_TARGET_ESP32P4", [1, 2, 3, 4], [0], 48, -1, 37, -1],
  "ATOMS3R" => [nil, [1, 2], [0], 2, 1, 45, 16],
  "STICKS3" => [nil, [1, 2], [0], 9, 10, 41, 13],
  "SIM" => [nil, [2], [0, 1], 47, 48, 43, -1],
}.freeze

pl_stub = File.join(ROOT, "test/api_fixes/pin_ledger/stub")
hal = File.join(ROOT, "components/fmrb_hal")
Dir.mktmpdir("app_uart") do |tmp|
  BOARDS.each do |board, (chip, app, system, tx, rx, sys_pin, irq)|
    exe = File.join(tmp, board)
    cmd = ["gcc", "-std=gnu11", "-Wall", "-Wno-unused-variable", "-Wno-unused-function", "-Werror",
           "-DTX_PIN=#{tx}", "-DRX_PIN=#{rx}", "-DSYS_PIN=#{sys_pin}", "-DIRQ_PIN=#{irq}"]
    if board == "SIM"
      cmd += ["-DCONFIG_IDF_TARGET_LINUX", "-I#{ROOT}/test/native/stub", "-I#{hal}",
              "-I#{ROOT}/components/fmrb_common/include", "-I#{ROOT}/main/app",
              "-o", exe, "#{__dir__}/uart_test.c", "#{hal}/platform/posix/fmrb_hal_pin_manager_posix.c"]
    else
      cmd << "-DFMRB_HW_#{board}" unless board == "NARYAV3" # the default branch
      cmd << "-D#{chip}" if chip
      cmd += ["-I#{pl_stub}", "-I#{hal}", "-I#{ROOT}/components/fmrb_common/include", "-I#{ROOT}/main/app",
              "-o", exe, "#{__dir__}/uart_test.c", "#{hal}/platform/esp32/fmrb_hal_pin_manager_esp32.c"]
    end
    out, st = Open3.capture2e(*cmd)
    check("uart #{board}: builds", [st.success?, out], [true, ""])
    next unless st.success?

    out, st = Open3.capture2e(exe)
    nums = ->(label) { (out[/^#{label}:(.*)$/, 1] || "").split.map(&:to_i) }
    check("uart #{board}: checks", [st.success?, out.lines.grep(/^FAIL/)], [true, []])
    check("uart #{board}: app units", nums.call("app"), app)
    check("uart #{board}: system units", nums.call("system"), system)
  end
end

# ---- Ruby: UART over a stand-in for FmrbUart --------------------------------

# The C side's rules, as far as the wrapper can tell them apart: units 0-4
# (2 is the apps', 0 and 1 the system's), one owner, a ring that drops the
# newest bytes, a read whose glue hands back at most 100 bytes a call (so the
# wrapper's loop is exercised), the 4096-byte write limit.
module FmrbUart
  RING = {}

  def self.reset
    @open = {}
    @wire = {}
    @calls = []
  end

  def self.calls = @calls
  def self.feed(unit, bytes) = deliver(unit, bytes)

  def self.deliver(unit, bytes)
    r = @open[unit] or return
    bytes.each_byte do |b|
      if r[:buf].bytesize < r[:size]
        r[:buf] << b.chr
      else
        r[:dropped] += 1
      end
    end
  end

  def self.open(unit, tx, rx, baud, data, stop, parity, size)
    @calls << [:open, unit, tx, rx, baud, data, stop, parity, size]
    return 1 if unit < 0 || unit > 4
    return 2 + 256 if tx < -1 || tx > 48
    return 3 if tx < 0 && rx < 0
    return 4 if tx == rx
    return 5 if baud < 300 || baud > 5_000_000
    return 7 if size < 16 || size > 16384 || (size & (size - 1)) != 0
    return 8 if unit < 2
    return 11 + 256 * 2 if rx == 43
    return 9 if @open[unit] && @open[unit][:owner] != :me
    @open[unit] = { owner: :me, buf: "".b, size: size, dropped: 0 }
    0
  end

  def self.slot(unit)
    @open[unit] or raise RuntimeError, "read failed: -1"
  end

  def self.close(unit) = !@open.delete(unit).nil?

  def self.read(unit, max)
    r = slot(unit)
    n = [max, r[:buf].bytesize, 100].min
    s = r[:buf].byteslice(0, n)
    r[:buf] = r[:buf].byteslice(n, r[:buf].bytesize - n)
    s
  end

  def self.read_line(unit)
    r = slot(unit)
    i = r[:buf].index("\n")
    return nil unless i
    s = r[:buf].byteslice(0, i + 1)
    r[:buf] = r[:buf].byteslice(i + 1, r[:buf].bytesize - i - 1)
    s
  end

  def self.write(unit, data)
    slot(unit)
    raise ArgumentError, "write: argument out of range" if data.bytesize > 4096
    @calls << [:write, data.bytesize]
    deliver(unit, data) # the loopback
    data.bytesize
  end

  def self.bytes_available(unit) = slot(unit)[:buf].bytesize

  def self.unget(unit, byte)
    r = slot(unit)
    return false if r[:buf].bytesize >= r[:size]
    r[:buf] = byte.chr + r[:buf]
    true
  end

  def self.flush(unit) = !slot(unit).nil?
  def self.clear_rx(unit) = (slot(unit)[:buf] = "".b; nil)
  def self.clear_tx(unit) = (slot(unit); nil)
  def self.send_break(unit, ms) = (slot(unit); @calls << [:break, ms]; nil)
  def self.set_baudrate(unit, baud) = (slot(unit); baud)
  def self.set_format(unit, d, s, p) = (slot(unit); @calls << [:format, d, s, p]; true)
  def self.dropped(unit) = slot(unit)[:dropped]
  def self.unit_mask = 4
end
FmrbUart.reset

load File.join(ROOT, "lib/gems/picoruby-fmrb-app/mrblib/fmrb-uart.rb")

u = UART.new(unit: :ESP32_UART2, txd_pin: 47, rxd_pin: 48, baudrate: 115200)
check("open's arguments", FmrbUart.calls.first, [:open, 2, 47, 48, 115200, 8, 1, 0, 4096])
check("unit number", u.instance_variable_get(:@unit_num), 2)
check("baudrate", u.baudrate, 115200)
check("nothing yet: read", u.read, nil)
check("nothing yet: gets", u.gets, nil)
check("nothing yet: getbyte", u.getbyte, nil)
check("nothing yet: readpartial", u.readpartial(10), nil)

check("puts", u.puts("hello"), nil)
check("puts adds the line ending", u.bytes_available, 6)
check("gets", u.gets, "hello\n")
u.write("ab")
check("gets waits for the end", u.gets, nil)
u.write("c\r\nd")
check("gets with CR", u.gets, "abc\r\n")
check("read(len) short", u.read(2), nil)
check("read(len) left it", u.bytes_available, 1)
check("read all", u.read, "d")

u.line_ending = "\r\n"
u.puts("x")
check("puts with CRLF", u.read, "x\r\n")
u.puts("y\r\n")
check("puts does not double", u.read, "y\r\n")
check("bad line ending", raised { u.line_ending = "\t" }, ["ArgumentError", "UART: invalid line ending"])

big = "z" * 300
u.write(big)
check("read gathers the glue's pieces", u.read(250), "z" * 250)
check("readpartial caps", u.readpartial(20), "z" * 20)
check("readpartial gives what is there", u.readpartial(100), "z" * 30)

FmrbUart.calls.clear
u2 = UART.new(unit: 2, txd_pin: 47, rxd_pin: -1, rx_buffer_size: 16)
long = "q" * 9000
check("write returns the length", u2.write(long), 9000)
check("write splits at 4096", FmrbUart.calls.select { |c| c[0] == :write }.map { |c| c[1] }, [4096, 4096, 808])
check("a full ring drops the newest", [u2.bytes_available, u2.dropped], [16, 8984])
check("ungetbyte on a full ring", raised { u2.ungetbyte(1) }, ["IOError", "UART: rx buffer is full"])
u2.clear_rx_buffer
check("cleared", u2.bytes_available, 0)
check("ungetbyte", u2.ungetbyte(0x141), nil)
check("ungetbyte keeps 8 bits", u2.getbyte, 0x41)

check("putc Integer", u2.putc(0x142), 0x142)
check("putc Integer sends a byte", u2.read, "B")
check("putc String sends one character", [u2.putc("あいう"), u2.read], ["あいう", "あ".b])
check("putc empty", u2.putc(""), "")
check("putc nil", raised { u2.putc(nil) }, ["TypeError", "wrong argument type (expected Integer or String)"])

FmrbUart.calls.clear
u2.break
u2.break(0.05)
u2.break(20)
check("break times", FmrbUart.calls.map { |c| c[1] }, [100, 50, 20])
check("break type", raised { u2.break("x") }, ["TypeError", "can't convert into time interval"])

check("setmode keeps the object", u2.setmode(baudrate: 9600).equal?(u2), true)
check("setmode baud", u2.baudrate, 9600)
check("setmode bad baud", raised { u2.setmode(baudrate: 10) },
      ["ArgumentError", "UART: baudrate must be from 300 to 5000000"])
check("set_format all nil", u2.set_format(nil, nil, nil), nil)
check("set_format some nil", raised { u2.set_format(8, nil, 0) },
      ["ArgumentError", "UART: data_bits, stop_bits and parity must be specified together"])
check("set_format range", raised { u2.set_format(9, 1, 0) }[0], "ArgumentError")
check("flow control", raised { u2.set_flow_control(UART::FLOW_CONTROL_RTS_CTS, 1, 2) }[0], "ArgumentError")
check("flow control none", u2.set_flow_control(UART::FLOW_CONTROL_NONE, -1, -1), nil)
check("flush", u2.flush.equal?(u2), true)
check("clear_tx_buffer", u2.clear_tx_buffer.equal?(u2), true)

check("close", [u2.closed?, u2.close, u2.closed?, u2.close], [false, nil, true, nil])
check("closed read", raised { u2.read }, ["IOError", "UART: closed"])
check("closed write", raised { u2.write("x") }, ["IOError", "UART: closed"])

# Opening a unit again: the object from before is closed, and touches the
# new port neither by reading, writing nor closing (doc/release_3_1 r1).
FmrbUart.reset
a = UART.new(unit: 2, txd_pin: 47, rxd_pin: 48)
check("first open", a.closed?, false)
b = UART.new(unit: 2, txd_pin: 47, rxd_pin: 48)
check("reopened: the old one is closed", [a.closed?, b.closed?], [true, false])
check("reopened: old write", raised { a.write("x") }, ["IOError", "UART: closed"])
check("reopened: old read", raised { a.read }, ["IOError", "UART: closed"])
check("reopened: old gets", raised { a.gets }, ["IOError", "UART: closed"])
check("reopened: old bytes_available", raised { a.bytes_available }, ["IOError", "UART: closed"])
check("reopened: old setmode", raised { a.setmode(baudrate: 9600) }, ["IOError", "UART: closed"])
check("reopened: old dropped", raised { a.dropped }, ["IOError", "UART: closed"])
FmrbUart.calls.clear
check("reopened: old close", [a.close, a.closed?], [nil, true])
check("reopened: old close leaves the port", [FmrbUart.calls, b.closed?], [[], false])
b.puts("new")
check("reopened: the new one works", b.gets, "new\n")
# A refused reopen leaves the one there is open (the C side keeps it).
check("refused reopen", raised { UART.new(unit: 2, txd_pin: 47, rxd_pin: 43) }[0], "IOError")
check("refused reopen keeps the open one", [b.closed?, b.bytes_available], [false, 0])
b.close
c = UART.new(unit: 2, txd_pin: 47, rxd_pin: 48)
check("closed, opened again", [b.closed?, c.closed?], [true, false])
check("closing the closed one again", [b.close, c.closed?], [nil, false])
c.close

# The answers of open as exceptions (fmrb_app_uart.h).
FmrbUart.reset
check("unknown name", raised { UART.new(unit: :RP2040_UART0, txd_pin: 1) },
      ["ArgumentError", "UART: unknown unit RP2040_UART0"])
check("no unit", raised { UART.new(unit: 9, txd_pin: 1) }, ["ArgumentError", "UART: no UART9 on this board"])
check("no pin", raised { UART.new(unit: 2, txd_pin: 99) },
      ["ArgumentError", "UART: TX pin 99 does not exist on this board"])
check("no pins", raised { UART.new(unit: 2) }, ["ArgumentError", "UART: give txd_pin, rxd_pin or both"])
check("same pin", raised { UART.new(unit: 2, txd_pin: 5, rxd_pin: 5) },
      ["ArgumentError", "UART: txd_pin and rxd_pin are the same pin"])
check("rx buffer", raised { UART.new(unit: 2, txd_pin: 5, rx_buffer_size: 100) }[0], "ArgumentError")
check("flow control at open", raised { UART.new(unit: 2, txd_pin: 5, flow_control: 1) }[0], "ArgumentError")
check("system unit", raised { UART.new(unit: 0, txd_pin: 5) }, ["IOError", "UART: UART0 is used by the system"])
check("system pin", raised { UART.new(unit: 2, txd_pin: 5, rxd_pin: 43) },
      ["IOError", "UART: RX pin 43 is used by the system"])
check("refusal texts", [9, 10, 10 + 256, 12 + 256, 13 + 512, 14, 15].map { |r| raised { UART._refuse(r, 2, 47, 48) } },
      [["IOError", "UART: UART2 is in use by another app"],
       ["IOError", "UART: UART2 is used by the MIDI serial output"],
       ["IOError", "UART: TX pin 47 is used by the MIDI serial output"],
       ["IOError", "UART: TX pin 47 is input only (a chip's interrupt line)"],
       ["IOError", "UART: RX pin 48 is in use by another app or driver"],
       ["IOError", "UART: UART2 could not be opened"],
       ["IOError", "UART: not available in this environment"]])

# ---- Upstream's test, its unit name replaced --------------------------------

module Picotest
  class Test
    def assert_equal(want, got) = check("upstream #{@name}", got, want)
    def assert_nil(got) = check("upstream #{@name}", got, nil)

    def assert_raise(klass)
      yield
      check("upstream #{@name} raises", nil, klass)
    rescue klass
      check("upstream #{@name} raises", klass, klass)
    end
  end
end

src = File.read(File.join(ROOT, "components/picoruby-esp32/picoruby/mrbgems/picoruby-uart/test/uart_test.rb"))
# The RP2040's UART0 there; unit 2 is the one an app has on every board here.
src = src.sub(":PICORB_UART_RP2040_UART0", ":ESP32_UART2, txd_pin: 47, rxd_pin: 48")
         .sub("assert_equal 0, @uart.instance_variable_get(:@unit_num)",
              "assert_equal 2, @uart.instance_variable_get(:@unit_num)")
eval(src, TOPLEVEL_BINDING, "uart_test.rb")
tests = UARTTest.instance_methods(false).grep(/\Atest_/).sort
check("upstream tests found", tests.size, 6)
tests.each do |t|
  FmrbUart.reset
  obj = UARTTest.new
  obj.instance_variable_set(:@name, t)
  obj.setup
  obj.send(t)
end

if $failures.zero?
  puts "app_uart: #{$count} checks passed"
else
  puts "app_uart: #{$failures} of #{$count} checks failed"
  exit 1
end
