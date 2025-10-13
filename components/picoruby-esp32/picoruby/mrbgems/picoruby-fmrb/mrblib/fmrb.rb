# Family mruby OS - Ruby interface

class FMRB
  # GPIO helper class
  class GPIO
    attr_reader :pin, :mode

    def initialize(pin, mode = FmrbHAL::GPIO_MODE_OUTPUT, pull = FmrbHAL::GPIO_PULL_NONE)
      @pin = pin
      @mode = mode
      ret = FmrbHAL.gpio_config(pin, mode, pull)
      raise "GPIO config failed: #{ret}" unless ret == FmrbHAL::OK
    end

    def write(level)
      ret = FmrbHAL.gpio_set(@pin, level)
      raise "GPIO write failed: #{ret}" unless ret == FmrbHAL::OK
    end

    def read
      level = FmrbHAL.gpio_get(@pin)
      raise "GPIO read failed: #{level}" if level < 0
      level
    end

    def high
      write(1)
    end

    def low
      write(0)
    end

    def toggle
      current = read
      write(current == 0 ? 1 : 0)
    end
  end

  # Time utilities
  class Time
    def self.now_ms
      FmrbHAL.time_ms
    end

    def self.delay(ms)
      FmrbHAL.delay_ms(ms)
    end

    def self.sleep(seconds)
      delay((seconds * 1000).to_i)
    end
  end

  # Graphics utilities (will be expanded when graphics binding is implemented)
  class Graphics
    def self.init(width = 320, height = 240)
      # TODO: Call graphics initialization
      puts "Graphics initialized: #{width}x#{height}"
    end

    def self.clear(color = 0x000000)
      # TODO: Call graphics clear
      puts "Graphics cleared with color: 0x#{color.to_s(16)}"
    end

    def self.draw_text(x, y, text, color = 0xFFFFFF)
      # TODO: Call graphics text drawing
      puts "Draw text at (#{x}, #{y}): #{text}"
    end
  end

  # Audio utilities (will be expanded when audio binding is implemented)
  class Audio
    def self.init(sample_rate = 44100, channels = 2)
      # TODO: Call audio initialization
      puts "Audio initialized: #{sample_rate}Hz, #{channels} channels"
    end

    def self.play(data)
      # TODO: Call audio play
      puts "Playing audio data (#{data.size} bytes)"
    end

    def self.set_volume(volume)
      # TODO: Call audio volume set
      puts "Set volume: #{volume}"
    end
  end

  # System initialization
  def self.init
    ret = FmrbHAL.init
    raise "HAL initialization failed: #{ret}" unless ret == FmrbHAL::OK
    puts "Family mruby HAL initialized"
  end

  def self.deinit
    FmrbHAL.deinit
    puts "Family mruby HAL deinitialized"
  end
end