# RX8130CE RTC driver (pure Ruby, uses I2C gem)
# Used on M5Stack Tab5 (Modern). Same I2C address as RX8900 (0x32) but a
# different register map: time registers start at 0x10.
# Provides the same public interface as RX8900 (init / read_time /
# write_time / sync_system_clock / vlf?).

class RX8130
  I2C_ADDR = 0x32

  # Register addresses
  REG_SEC    = 0x10
  REG_MIN    = 0x11
  REG_HOUR   = 0x12
  REG_WEEK   = 0x13
  REG_DAY    = 0x14
  REG_MONTH  = 0x15
  REG_YEAR   = 0x16
  REG_EXT    = 0x1C
  REG_FLAG   = 0x1D
  REG_CTRL0  = 0x1E
  REG_CTRL1  = 0x1F

  FLAG_VLF   = 0x02  # voltage low: time data is invalid
  CTRL0_STOP = 0x40

  def initialize(i2c)
    @i2c = i2c
  end

  # Initialize RX8130: enable battery backup charge (CTRL1 bits 4/5,
  # same as the M5Stack Tab5 factory demo) and make sure the clock is
  # running. Does NOT clear VLF: a set VLF must stay visible so a
  # power-lost RTC is not trusted until time is written again.
  def init
    val = byte_read(REG_CTRL1)
    byte_write(REG_CTRL1, val | 0x30)
    val = byte_read(REG_CTRL0)
    byte_write(REG_CTRL0, val & ~(CTRL0_STOP | 0x80))  # clear STOP/TEST
  end

  # Read current time from RTC
  # Returns Hash: {year:, month:, day:, hour:, minute:, second:, wday:}
  def read_time
    data = @i2c.read(I2C_ADDR, 7, REG_SEC)
    return nil unless data && data.length >= 7

    {
      second: bcd2dec(data.getbyte(0) & 0x7F),
      minute: bcd2dec(data.getbyte(1) & 0x7F),
      hour:   bcd2dec(data.getbyte(2) & 0x3F),
      wday:   data.getbyte(3),
      day:    bcd2dec(data.getbyte(4) & 0x3F),
      month:  bcd2dec(data.getbyte(5) & 0x1F),
      year:   2000 + bcd2dec(data.getbyte(6))
    }
  end

  # Write time to RTC
  # h: Hash with keys :year, :month, :day, :hour, :minute, :second
  def write_time(h)
    year_offset = h[:year] - 2000
    wday_bit = 1 << zeller(h[:year], h[:month], h[:day])

    # Stop the clock while updating the calendar registers
    val = byte_read(REG_CTRL0)
    byte_write(REG_CTRL0, val | CTRL0_STOP)

    byte_write(REG_SEC,   dec2bcd(h[:second]))
    byte_write(REG_MIN,   dec2bcd(h[:minute]))
    byte_write(REG_HOUR,  dec2bcd(h[:hour]))
    byte_write(REG_WEEK,  wday_bit)
    byte_write(REG_DAY,   dec2bcd(h[:day]))
    byte_write(REG_MONTH, dec2bcd(h[:month]))
    byte_write(REG_YEAR,  dec2bcd(year_offset))

    byte_write(REG_CTRL0, val & ~CTRL0_STOP)

    # Time is valid now: clear VLF (and other event flags)
    byte_write(REG_FLAG, 0x00)
  end

  # Read time from RTC and set ESP-IDF system clock via Machine.set_hwclock
  def sync_system_clock
    return false if vlf?
    t = read_time
    return false unless t
    return false unless valid_time?(t)
    begin
      epoch = time_to_epoch(t[:year], t[:month], t[:day],
                            t[:hour], t[:minute], t[:second])
      Machine.set_hwclock(epoch)
      true
    rescue => e
      Log.error("RX8130 sync_system_clock failed: #{e.message}")
      false
    end
  end

  # Check voltage low flag (time data lost)
  def vlf?
    flag = byte_read(REG_FLAG)
    (flag & FLAG_VLF) != 0
  end

  private

  def valid_time?(t)
    t[:year] >= 2000 && t[:year] <= 2099 &&
      t[:month] >= 1 && t[:month] <= 12 &&
      t[:day] >= 1 && t[:day] <= 31 &&
      t[:hour] <= 23 && t[:minute] <= 59 && t[:second] <= 59
  end

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
    y = year
    m = month
    mdays = [0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]

    days = 0
    (1970...y).each do |yr|
      days += leap_year?(yr) ? 366 : 365
    end
    (1...m).each do |mo|
      days += mdays[mo]
      days += 1 if mo == 2 && leap_year?(y)
    end
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
