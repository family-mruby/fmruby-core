MRuby::Gem::Specification.new('picoruby-rx8900') do |spec|
  spec.license = 'MIT'
  spec.authors = ['Katsuhiko Kageyama']
  spec.summary = 'RX8900 RTC driver for PicoRuby (pure Ruby, uses I2C gem)'

  spec.add_dependency 'picoruby-i2c'
end
