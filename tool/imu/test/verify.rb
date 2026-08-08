#!/usr/bin/env ruby
# Host-side check of the BMI270 driver (lib/add/picoruby-bmi270).
#
# The driver is plain Ruby, so it can be loaded here and driven against a fake
# device that answers like a BMI270 and records everything written to it. What
# this pins down is the part that cannot be seen on the screen: the reset and
# enable sequence, and that the 8192-byte configuration image arrives at the
# sensor byte for byte, at the right load addresses.
#
#   ruby tool/imu/test/verify.rb
#
# Exits non-zero on the first difference.

ROOT = ::File.expand_path("../../..", __dir__)
DRIVER = ::File.join(ROOT, "lib/add/picoruby-bmi270/mrblib/bmi270.rb")
CONFIG = ::File.join(ROOT, "flash/usr/share/imu/bmi270_config.bin")

# The driver waits on the device with Machine.delay_ms; on the host there is
# nothing to wait for.
module Machine
  def self.delay_ms(ms); end
end

load DRIVER

# A BMI270 that answers on one address and remembers what it was told.
class FakeDevice
  REG_CHIP_ID         = 0x00
  REG_ACC_X_LSB       = 0x0C
  REG_INTERNAL_STATUS = 0x21
  REG_TEMPERATURE     = 0x22
  REG_INIT_CTRL       = 0x59
  REG_INIT_ADDR_0     = 0x5B
  REG_INIT_DATA       = 0x5E
  REG_PWR_CONF        = 0x7C
  REG_PWR_CTRL        = 0x7D
  REG_CMD             = 0x7E

  attr_reader :log, :uploaded, :chunk_sizes, :closed

  # chip_id:    what CHIP_ID answers (0x24 is a BMI270)
  # status:     what INTERNAL_STATUS answers after the load
  # nack_after_reset: how many reads to refuse once the soft reset lands, the
  #                   way the real part does while it is coming back up
  def initialize(addr: 0x68, chip_id: 0x24, status: 0x01, nack_after_reset: 0)
    @addr = addr
    @chip_id = chip_id
    @status = status
    @nack_left = 0
    @nack_after_reset = nack_after_reset
    @log = []            # [:write, reg, [bytes]] / [:read, reg, len]
    @uploaded = {}       # byte index -> byte value
    @chunk_sizes = []
    @load_index = nil
    @sensor = nil        # 12 bytes of accel+gyro, set by the test
    @closed = false
  end

  def sensor_bytes=(bytes)
    @sensor = bytes
  end

  def read(addr, len, reg = nil)
    raise IOError, "no device at 0x#{addr.to_s(16)}" unless addr == @addr
    if @nack_left > 0
      @nack_left -= 1
      raise IOError, "not ready"
    end
    @log << [:read, reg, len]
    case reg
    when REG_CHIP_ID         then [@chip_id].pack("C")
    when REG_INTERNAL_STATUS then [@status].pack("C")
    when REG_TEMPERATURE     then [0x00, 0x08].pack("C*")   # 23 + 2048/512 = 27 C
    when REG_ACC_X_LSB       then (@sensor || Array.new(12, 0)).pack("C*")
    else "\x00" * len
    end
  end

  def write(addr, *args)
    raise IOError, "no device at 0x#{addr.to_s(16)}" unless addr == @addr
    bytes = args.flatten
    reg = bytes.shift
    @log << [:write, reg, bytes]

    case reg
    when REG_CMD
      @nack_left = @nack_after_reset if bytes[0] == 0xB6
    when REG_INIT_ADDR_0
      # Load address counts 16-bit words: low nibble first, then the rest.
      words = (bytes[1] << 4) | (bytes[0] & 0x0F)
      @load_index = words * 2
    when REG_INIT_DATA
      raise "configuration data before an address" unless @load_index
      @chunk_sizes << bytes.size
      bytes.each_with_index { |b, i| @uploaded[@load_index + i] = b }
      @load_index += bytes.size
    end
    bytes.size + 1
  end

  def close
    @closed = true
  end

  def writes_to(reg)
    @log.select { |kind, r, _| kind == :write && r == reg }.map { |_, _, b| b }
  end

  def write_order
    @log.select { |kind, _, _| kind == :write }.map { |_, reg, _| reg }
  end
end

$failures = 0

def check(label)
  ok, detail = yield
  if ok
    puts "  ok   #{label}"
  else
    puts "  FAIL #{label}#{detail ? " -- #{detail}" : ""}"
    $failures += 1
  end
rescue => e
  puts "  FAIL #{label} -- #{e.class}: #{e.message}"
  $failures += 1
end

config = ::File.open(CONFIG, "rb") { |f| f.read }

puts "configuration image"
check("is #{BMI270::CONFIG_SIZE} bytes") do
  [config.bytesize == BMI270::CONFIG_SIZE, "got #{config.bytesize}"]
end

puts "probe"
[0x68, 0x69].each do |addr|
  check("finds the sensor at 0x#{addr.to_s(16)}") do
    imu = BMI270.new(FakeDevice.new(addr: addr))
    [imu.probe == addr, "got #{imu.probe.inspect}"]
  end
end
check("returns nil when nothing answers as a BMI270") do
  imu = BMI270.new(FakeDevice.new(addr: 0x68, chip_id: 0x11))
  [imu.probe.nil?, "got #{imu.probe.inspect}"]
end

puts "init"
device = FakeDevice.new
imu = BMI270.new(device)
result = imu.init(CONFIG)

check("reports success") { [result == true, "got #{result.inspect}"] }
check("marks itself ready") { [imu.ready?, nil] }
check("issues a soft reset") do
  [device.writes_to(FakeDevice::REG_CMD) == [[0xB6]], nil]
end
check("turns power saving off before the load") do
  [device.writes_to(FakeDevice::REG_PWR_CONF) == [[0x00]], nil]
end
check("brackets the load with INIT_CTRL 0 then 1") do
  [device.writes_to(FakeDevice::REG_INIT_CTRL) == [[0x00], [0x01]], nil]
end
check("enables temperature, accelerometer and gyroscope") do
  [device.writes_to(FakeDevice::REG_PWR_CTRL) == [[0x0E]], nil]
end
check("enables the sensors only after the load ends") do
  order = device.write_order
  [order.index(FakeDevice::REG_PWR_CTRL) > order.rindex(FakeDevice::REG_INIT_CTRL), nil]
end

puts "configuration upload"
check("delivers every byte at the right address") do
  rebuilt = "\x00".dup * BMI270::CONFIG_SIZE
  device.uploaded.each { |index, byte| rebuilt.setbyte(index, byte) }
  diff = (0...BMI270::CONFIG_SIZE).find { |i| rebuilt.getbyte(i) != config.getbyte(i) }
  [device.uploaded.size == BMI270::CONFIG_SIZE && diff.nil?,
   diff ? "first difference at byte #{diff}" : "wrote #{device.uploaded.size} bytes"]
end
check("uses even-sized chunks (the load address counts words)") do
  odd = device.chunk_sizes.reject { |n| n.even? }
  [odd.empty?, "odd chunks: #{odd.inspect}"]
end
# Modern routes every I2C transaction through the display driver's service,
# which takes the length in a byte and refuses anything past 255. The register
# byte rides in the same transaction, so the payload has 254 to live in. A
# chunk over that is rejected outright, which reads as an init that fails
# without ever touching the bus in anger.
check("keeps a whole transaction inside the 255-byte bus limit") do
  worst = device.chunk_sizes.max + 1
  [worst <= 255, "largest transaction is #{worst} bytes"]
end
check("splits the image into #{BMI270::CONFIG_SIZE / BMI270::CHUNK_SIZE} chunks") do
  [device.chunk_sizes.size == BMI270::CONFIG_SIZE / BMI270::CHUNK_SIZE,
   "got #{device.chunk_sizes.size}"]
end

puts "init on a part that is slow to come back"
# The real sensor refuses reads for the first few milliseconds after a soft
# reset. A single attempt there fails on hardware while passing every test
# against a fake that always answers, which is exactly what happened.
check("keeps asking until the sensor answers after the reset") do
  slow_imu = BMI270.new(FakeDevice.new(nack_after_reset: 6))
  [slow_imu.init(CONFIG) == true, "error: #{slow_imu.error.inspect}"]
end
check("gives up when it never answers after the reset") do
  dead_imu = BMI270.new(FakeDevice.new(nack_after_reset: 10_000))
  [dead_imu.init(CONFIG) == false && dead_imu.error == "no answer after reset",
   "error: #{dead_imu.error.inspect}"]
end

puts "init failure cases"
check("gives up when the sensor never reports a good load") do
  bad = FakeDevice.new(status: 0x00)
  [BMI270.new(bad).init(CONFIG) == false, nil]
end
check("gives up when the configuration file is missing") do
  [BMI270.new(FakeDevice.new).init("/nonexistent/bmi270_config.bin") == false, nil]
end
check("gives up when the configuration file is the wrong size") do
  require "tempfile"
  short = Tempfile.new("bmi270")
  short.write("\x00" * 16)
  short.close
  [BMI270.new(FakeDevice.new).init(short.path) == false, nil]
end
check("gives up when no sensor answers") do
  [BMI270.new(FakeDevice.new(chip_id: 0x00)).init(CONFIG) == false, nil]
end

puts "measurements"
# accel x = +1000, y = -1000, z = +16384 (1 g at +-8 g full scale)
# gyro  x = -1, y = +2, z = -32768
raw = [0xE8, 0x03, 0x18, 0xFC, 0x00, 0x40,
       0xFF, 0xFF, 0x02, 0x00, 0x00, 0x80]
device.sensor_bytes = raw

check("converts acceleration to g") do
  a = imu.read_accel
  res = 8.0 / 32768.0
  [(a[:x] - 1000 * res).abs < 1e-6 &&
   (a[:y] + 1000 * res).abs < 1e-6 &&
   (a[:z] - 0.5 * 8.0).abs < 1e-6, a.inspect]
end
check("converts angular rate to degrees per second") do
  g = imu.read_gyro
  res = 2000.0 / 32768.0
  [(g[:x] + res).abs < 1e-6 &&
   (g[:y] - 2 * res).abs < 1e-6 &&
   (g[:z] + 2000.0).abs < 1e-6, g.inspect]
end
check("reads negative counts as negative") do
  r = imu.read_raw
  [r[:ay] == -1000 && r[:gx] == -1 && r[:gz] == -32768, r.inspect]
end
check("reads the die temperature") do
  t = imu.read_temperature
  [(t - 27.0).abs < 1e-6, t.inspect]
end

puts
if $failures.zero?
  puts "all checks passed"
  exit 0
else
  puts "#{$failures} check(s) failed"
  exit 1
end
