MRuby::Gem::Specification.new('picoruby-bmi270') do |spec|
  spec.license = 'MIT'
  spec.authors = ['Katsuhiko Kageyama']
  spec.summary = 'BMI270 six-axis IMU driver for PicoRuby (pure Ruby, uses I2C gem) - M5Stack Tab5'

  spec.add_dependency 'picoruby-i2c'
end
