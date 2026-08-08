# BMI270 six-axis IMU driver (pure Ruby, uses the I2C gem).
# Used on M5Stack Tab5 (Modern), where the part shares the internal I2C bus
# with the touch controller, the IO expanders, the codec and the RTC. That bus
# is serialized by the display driver, so ordinary I2C calls from an app are
# safe here.
#
# The sensor answers on the bus straight after reset but produces no
# measurement until an 8192-byte configuration image is written into its
# internal memory. The image is a file in the filesystem rather than a Ruby
# literal, so it stays out of the firmware and can be streamed in chunks:
#
#   i2c = I2C.new(unit: :ESP32_I2C1, sda_pin: ..., scl_pin: ...)
#   imu = BMI270.new(i2c)
#   imu.init
#   a = imu.read_accel   # {x:, y:, z:} in g

class BMI270
  CHIP_ID       = 0x24
  DEFAULT_ADDRS = [0x68, 0x69]
  CONFIG_PATH   = "/usr/share/imu/bmi270_config.bin"
  CONFIG_SIZE   = 8192
  # The load address counts 16-bit words, so a chunk must be an even number of
  # bytes. 256 keeps the number of transactions (32) and the temporary array
  # both small.
  CHUNK_SIZE    = 256

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

  CMD_SOFT_RESET = 0xB6
  PWR_CTRL_ON    = 0x0E   # temperature + accelerometer + gyroscope enabled
  INIT_OK        = 0x01   # low nibble of INTERNAL_STATUS after a good load

  # Full-scale defaults that a configuration load leaves behind.
  ACC_RANGE_G   = 8.0
  GYR_RANGE_DPS = 2000.0

  # Temperature register: 23 degC at zero, 1/512 degC per count.
  TEMP_OFFSET_C = 23.0
  TEMP_STEP_C   = 512.0

  attr_reader :address

  # i2c:  an open I2C instance
  # addr: skip the probe and use this address (0x68 or 0x69)
  def initialize(i2c, addr = nil)
    @i2c = i2c
    @address = addr
    @ready = false
  end

  def ready?
    @ready
  end

  # Look for the sensor and remember where it answered. Returns the address,
  # or nil when nothing on the bus identifies itself as a BMI270.
  def probe
    addrs = @address ? [@address] : DEFAULT_ADDRS
    addrs.each do |addr|
      begin
        data = @i2c.read(addr, 1, REG_CHIP_ID)
        if data && data.bytesize >= 1 && data.getbyte(0) == CHIP_ID
          @address = addr
          return addr
        end
      rescue
        # nothing at this address; try the next one
      end
    end
    nil
  end

  # Reset the sensor, upload the configuration image and start the
  # accelerometer and the gyroscope. Returns true once the sensor reports a
  # good load.
  def init(config_path = CONFIG_PATH)
    @ready = false
    return false unless probe

    config = load_config(config_path)
    return false unless config

    write8(REG_CMD, CMD_SOFT_RESET)
    ::Machine.delay_ms(5)
    # A read after reset puts the interface back into I2C mode.
    begin
      @i2c.read(@address, 1, REG_CHIP_ID)
    rescue
      return false
    end

    write8(REG_PWR_CONF, 0x00)   # power saving off, otherwise the load fails
    ::Machine.delay_ms(1)
    write8(REG_INIT_CTRL, 0x00)  # start of the configuration load

    return false unless upload(config)

    write8(REG_INIT_CTRL, 0x01)  # end of the configuration load

    # The sensor needs up to 20 ms to digest the image.
    ok = false
    tries = 0
    while tries < 20
      ::Machine.delay_ms(2)
      status = read8(REG_INTERNAL_STATUS)
      if status && (status & 0x0F) == INIT_OK
        ok = true
        break
      end
      tries += 1
    end
    return false unless ok

    write8(REG_PWR_CTRL, PWR_CTRL_ON)
    ::Machine.delay_ms(2)
    @ready = true
    true
  end

  # Raw signed counts: {ax:, ay:, az:, gx:, gy:, gz:}
  def read_raw
    data = begin
      @i2c.read(@address, 12, REG_ACC_X_LSB)
    rescue
      nil
    end
    return nil unless data && data.bytesize >= 12

    {
      ax: s16(data, 0), ay: s16(data, 2), az: s16(data, 4),
      gx: s16(data, 6), gy: s16(data, 8), gz: s16(data, 10)
    }
  end

  # Acceleration in g: {x:, y:, z:}
  def read_accel
    raw = read_raw
    return nil unless raw
    res = ACC_RANGE_G / 32768.0
    { x: raw[:ax] * res, y: raw[:ay] * res, z: raw[:az] * res }
  end

  # Angular rate in degrees per second: {x:, y:, z:}
  def read_gyro
    raw = read_raw
    return nil unless raw
    res = GYR_RANGE_DPS / 32768.0
    { x: raw[:gx] * res, y: raw[:gy] * res, z: raw[:gz] * res }
  end

  # Die temperature in degrees Celsius. Reads warm: it is the sensor, not the
  # room.
  def read_temperature
    data = begin
      @i2c.read(@address, 2, REG_TEMPERATURE)
    rescue
      nil
    end
    return nil unless data && data.bytesize >= 2
    TEMP_OFFSET_C + s16(data, 0) / TEMP_STEP_C
  end

  private

  def load_config(path)
    data = nil
    begin
      ::File.open(path, "r") { |f| data = f.read }
    rescue
      return nil
    end
    return nil unless data && data.bytesize == CONFIG_SIZE
    data
  end

  # Write the image in chunks. Before each chunk the load address is set in
  # 16-bit words: low nibble in 0x5B, the rest in 0x5C.
  def upload(config)
    index = 0
    while index < CONFIG_SIZE
      words = index >> 1
      begin
        @i2c.write(@address, REG_INIT_ADDR_0, [words & 0x0F, words >> 4])
        @i2c.write(@address, REG_INIT_DATA, chunk_at(config, index))
      rescue
        return false
      end
      index += CHUNK_SIZE
    end
    true
  end

  # Byte-wise slicing: the strings are UTF-8 aware, so String#[] would count
  # characters instead of bytes on this data.
  def chunk_at(config, index)
    bytes = []
    i = 0
    while i < CHUNK_SIZE && (index + i) < CONFIG_SIZE
      bytes << config.getbyte(index + i)
      i += 1
    end
    bytes
  end

  def write8(reg, value)
    @i2c.write(@address, reg, value)
  end

  def read8(reg)
    data = begin
      @i2c.read(@address, 1, reg)
    rescue
      nil
    end
    data && data.bytesize >= 1 ? data.getbyte(0) : nil
  end

  def s16(data, offset)
    value = data.getbyte(offset) | (data.getbyte(offset + 1) << 8)
    value >= 0x8000 ? value - 0x10000 : value
  end
end
