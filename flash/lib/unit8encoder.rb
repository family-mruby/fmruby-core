# M5Stack Unit 8Encoder driver (pure Ruby, uses I2C gem)
# I2C device with 8 rotary encoders, 8 buttons, 1 switch, and 9 RGB LEDs

class Unit8Encoder
  DEFAULT_ADDR = 0x41
  NUM_ENCODERS = 8
  NUM_LEDS     = 9

  # Register addresses
  REG_COUNTER   = 0x00  # 8ch x 4bytes, int32, R/W
  REG_INCREMENT = 0x20  # 8ch x 4bytes, int32, R (reset after get)
  REG_RESET     = 0x40  # 8ch x 1byte, W
  REG_BUTTON    = 0x50  # 8ch x 1byte, R
  REG_SWITCH    = 0x60  # 1byte, R
  REG_RGB_LED   = 0x70  # 9 LEDs x 3bytes (R,G,B), R/W
  REG_FW_VER    = 0xFE  # 1byte, R
  REG_I2C_ADDR  = 0xFF  # 1byte, R/W

  def initialize(i2c, addr = DEFAULT_ADDR)
    @i2c = i2c
    @addr = addr
  end

  # Get encoder counter value (int32)
  # index: 0..7
  def get_counter(index)
    reg = index * 4 + REG_COUNTER
    data = @i2c.read(@addr, 4, reg)
    bytes_to_int32(data)
  end

  # Set encoder counter value (int32)
  # index: 0..7, value: Integer
  def set_counter(index, value)
    reg = index * 4 + REG_COUNTER
    @i2c.write(@addr, reg, int32_to_bytes(value))
  end

  # Get increment value since last read (int32, auto-reset)
  # index: 0..7
  def get_increment(index)
    reg = index * 4 + REG_INCREMENT
    data = @i2c.read(@addr, 4, reg)
    bytes_to_int32(data)
  end

  # Reset counter for specified encoder
  # index: 0..7
  def reset_counter(index)
    reg = index + REG_RESET
    @i2c.write(@addr, reg, 1)
  end

  # Get button state (true = pressed)
  # index: 0..7
  def get_button(index)
    reg = index + REG_BUTTON
    data = @i2c.read(@addr, 1, reg)
    data.getbyte(0) != 0
  end

  # Get switch state (true = on)
  def get_switch
    data = @i2c.read(@addr, 1, REG_SWITCH)
    data.getbyte(0) != 0
  end

  # Set LED color
  # index: 0..8, color: 0xRRGGBB
  def set_led_color(index, color)
    r = (color >> 16) & 0xFF
    g = (color >> 8) & 0xFF
    b = color & 0xFF
    reg = index * 3 + REG_RGB_LED
    @i2c.write(@addr, reg, [r, g, b])
  end

  # Set all LEDs to the same color
  # color: 0xRRGGBB
  def set_all_led_color(color)
    r = (color >> 16) & 0xFF
    g = (color >> 8) & 0xFF
    b = color & 0xFF
    data = []
    NUM_LEDS.times do
      data << r
      data << g
      data << b
    end
    @i2c.write(@addr, REG_RGB_LED, data)
  end

  # Get firmware version
  def firmware_version
    data = @i2c.read(@addr, 1, REG_FW_VER)
    data.getbyte(0)
  end

  # Get current I2C address
  def get_i2c_address
    data = @i2c.read(@addr, 1, REG_I2C_ADDR)
    data.getbyte(0)
  end

  # Set new I2C address (1..127)
  def set_i2c_address(new_addr)
    @i2c.write(@addr, REG_I2C_ADDR, new_addr)
    @addr = new_addr
  end

  private

  # Convert 4-byte little-endian string to signed int32
  def bytes_to_int32(data)
    value = data.getbyte(0) |
            (data.getbyte(1) << 8) |
            (data.getbyte(2) << 16) |
            (data.getbyte(3) << 24)
    # Sign extension
    value >= 0x80000000 ? value - 0x100000000 : value
  end

  # Convert signed int32 to 4-byte little-endian array
  def int32_to_bytes(value)
    value = value & 0xFFFFFFFF
    [
      value & 0xFF,
      (value >> 8) & 0xFF,
      (value >> 16) & 0xFF,
      (value >> 24) & 0xFF
    ]
  end
end
