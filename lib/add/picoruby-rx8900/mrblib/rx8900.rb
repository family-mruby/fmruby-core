# RX8900 RTC driver (pure Ruby, uses I2C gem)
# Ported from the C driver (main/drivers/rtc/rx8900.c)

class RX8900
  I2C_ADDR = 0x32

  # Register addresses
  REG_SEC       = 0x00
  REG_MIN       = 0x01
  REG_HOUR      = 0x02
  REG_WEEK      = 0x03
  REG_DAY       = 0x04
  REG_MONTH     = 0x05
  REG_YEAR      = 0x06
  REG_EXTENSION = 0x0D
  REG_FLAG      = 0x0E
  REG_CONTROL   = 0x0F
  REG_TEMP      = 0x17

  def initialize(i2c)
    @i2c = i2c
  end

  # Initialize RX8900 registers (WEEK ALARM mode, 1Hz FOUT)
  def init
    byte_write(REG_EXTENSION, 0x08)
    byte_write(REG_FLAG, 0x00)
    byte_write(REG_CONTROL, 0x40)  # CSEL1=1 (1Hz FOUT)
  end

  # Read current time from RTC
  # Returns Hash: {year:, month:, day:, hour:, minute:, second:, wday:}
  def read_time
    # Burst read 7 bytes from REG_SEC (0x00)
    data = @i2c.read(I2C_ADDR, 7, REG_SEC)
    return nil unless data && data.length >= 7

    {
      second: bcd2dec(data.getbyte(0)),
      minute: bcd2dec(data.getbyte(1)),
      hour:   bcd2dec(data.getbyte(2)),
      wday:   data.getbyte(3),
      day:    bcd2dec(data.getbyte(4)),
      month:  bcd2dec(data.getbyte(5)),
      year:   2000 + bcd2dec(data.getbyte(6))
    }
  end

  # Write time to RTC
  # h: Hash with keys :year, :month, :day, :hour, :minute, :second
  def write_time(h)
    year_offset = h[:year] - 2000
    wday_bit = 1 << zeller(h[:year], h[:month], h[:day])

    # Reset sub-second counter
    val = byte_read(REG_CONTROL)
    byte_write(REG_CONTROL, val | 0x01)

    byte_write(REG_SEC,   dec2bcd(h[:second]))
    byte_write(REG_MIN,   dec2bcd(h[:minute]))
    byte_write(REG_HOUR,  dec2bcd(h[:hour]))
    byte_write(REG_WEEK,  wday_bit)
    byte_write(REG_DAY,   dec2bcd(h[:day]))
    byte_write(REG_MONTH, dec2bcd(h[:month]))
    byte_write(REG_YEAR,  dec2bcd(year_offset))
  end

  # Read time from RTC and set ESP-IDF system clock via Machine.set_hwclock
  def sync_system_clock
    t = read_time
    return false unless t
    begin
      epoch = time_to_epoch(t[:year], t[:month], t[:day],
                            t[:hour], t[:minute], t[:second])
      Machine.set_hwclock(epoch)
      true
    rescue => e
      Log.error("RX8900 sync_system_clock failed: #{e.message}")
      false
    end
  end

  # Read temperature from RTC (degrees Celsius)
  def temperature
    raw = byte_read(REG_TEMP)
    (raw * 2.0 - 187.19) / 3.218
  end

  # Check voltage low flag (battery issue)
  def vlf?
    flag = byte_read(REG_FLAG)
    (flag & 0x02) != 0
  end

  private

  def byte_write(reg, value)
    @i2c.write(I2C_ADDR, reg, value)
  end

  def byte_read(reg)
    data = @i2c.read(I2C_ADDR, 1, reg)
    data.getbyte(0)
  end

  def dec2bcd(n)
    n + 6 * (n / 10)
  end

  def bcd2dec(n)
    n - 6 * (n >> 4)
  end

  # Convert date/time to Unix epoch (UTC)
  def time_to_epoch(year, month, day, hour, minute, second)
    # Days from year 0 to year (simplified for 2000-2099)
    y = year
    m = month
    # Days in each month (non-leap year)
    mdays = [0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]

    # Calculate days since 1970-01-01
    days = 0
    # Years
    (1970...y).each do |yr|
      days += leap_year?(yr) ? 366 : 365
    end
    # Months
    (1...m).each do |mo|
      days += mdays[mo]
      days += 1 if mo == 2 && leap_year?(y)
    end
    # Days
    days += day - 1

    days * 86400 + hour * 3600 + minute * 60 + second
  end

  def leap_year?(y)
    (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)
  end

  # Zeller's congruence: returns 0=Sunday .. 6=Saturday
  def zeller(y, m, d)
    if m < 3
      y -= 1
      m += 12
    end
    (y + y / 4 - y / 100 + y / 400 + (13 * m + 8) / 5 + d) % 7
  end
end
