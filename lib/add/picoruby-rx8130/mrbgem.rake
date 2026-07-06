MRuby::Gem::Specification.new('picoruby-rx8130') do |spec|
  spec.license = 'MIT'
  spec.authors = ['Katsuhiko Kageyama']
  spec.summary = 'RX8130 RTC driver for PicoRuby (pure Ruby, uses I2C gem) - M5Stack Tab5'

  spec.add_dependency 'picoruby-i2c'
end
