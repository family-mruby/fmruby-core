# frozen_string_literal: true
#
# step_sensor's screen (flash/app/test/step_sensor.app.rb): the top line's
# WiFi ok / -- and AST ok / .. / --, the count, the cadence or "no IMU".
# A window on Modern and Retro; the small boards (240x135, 128x128) take
# the whole screen. The BMI270 is a stand-in that plays a walk (a rise of
# 0.5 g twice a second) or is not found.

module StepScenes
  module_function

  # The sensor's stand-ins: GPIO (the front button, up), I2C, BMI270.
  def hardware(found: true)
    Object.const_set(:FmrbHw, Module.new) unless Object.const_defined?(:FmrbHw)
    FmrbHw.const_set(:PIN_I2C1_SDA, 38)
    FmrbHw.const_set(:PIN_I2C1_SCL, 39)
    gpio = Class.new do
      def initialize(*) = nil
      def read = 1
    end
    gpio.const_set(:IN, 1)
    Object.const_set(:GPIO, gpio)
    Object.const_set(:I2C, Class.new { def initialize(**) = nil; def close = nil })
    bmi = Class.new do
      def initialize(_i2c, _addr = nil)
        @t = 0
      end

      def init(*) = self.class::FOUND
      def address = 0x68
      def error = "not found"
      def fifo_accel_start = nil

      # One poll's samples (POLL_MS = ODR_MS: one), z with a 2 Hz walk.
      def read_fifo_accel(*)
        @t += 1
        g = 32_768 / self.class::ACC_RANGE_G
        z = (g * (1.0 + (@t % 50 < 8 ? 0.5 : 0.0))).round
        [0, 0, z]
      end
    end
    bmi.const_set(:ACC_RANGE_G, 8.0)
    bmi.const_set(:FOUND, found)
    Object.const_set(:BMI270, bmi)
  end

  def wifi(s, on)
    s.env.wifi = on ? { connected: true, ip: "192.0.2.10", hostname: "fmruby-a1b2c3" } : { connected: false }
  end
end

Regress.app "step_sensor", "flash/app/test/step_sensor.app.rb" do
  prelude { |_s| StepScenes.hardware }

  scene "wifi_ok_ast_ok" do |s|
    s.start
    s.run(2600, max_ms: 10)
  end

  scene "wifi_ok_ast_connecting" do |s|
    s.start
    s.run(300, max_ms: 10)
    s.net.reconnecting = true
    s.run(300, max_ms: 10)
  end

  scene "wifi_ok_ast_off" do |s|
    s.net.refuse = "no route to the router"
    s.start
    s.run(300, max_ms: 10)
  end

  scene "wifi_off_ast_off" do |s|
    StepScenes.wifi(s, false)
    s.start
    s.run(300, max_ms: 10)
  end

  scene "wifi_off_ast_ok" do |s|
    s.start
    s.run(300, max_ms: 10)
    StepScenes.wifi(s, false)
    s.run(1200, max_ms: 10)
  end

  scene "wifi_off_ast_connecting" do |s|
    s.start
    s.run(300, max_ms: 10)
    StepScenes.wifi(s, false)
    s.net.reconnecting = true
    s.run(1200, max_ms: 10)
  end

  scene "no_imu" do |s|
    BMI270.send(:remove_const, :FOUND)
    BMI270.const_set(:FOUND, false)
    s.start
    s.run(300, max_ms: 10)
  end
end
