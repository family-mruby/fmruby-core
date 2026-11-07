# frozen_string_literal: true
#
# welcome's screen (flash/app/tool/welcome.app.rb), the startup_app of the
# small boards (StickS3 240x135, AtomS3R 128x128): the title with the
# spinner, the version and board, Wi-Fi connected (IP and mDNS name) /
# connecting (SSID) / off with the next-step hint, the uptime.
# Drawn on the small boards only, which are what it is for.

Regress.app "welcome", "flash/app/tool/welcome.app.rb" do
  on { |t| t.family == "mini" }

  scene "connected" do |s|
    s.start
    s.run(1500)
  end

  scene "connecting" do |s|
    s.env.wifi = { connected: false, ssid: "fmrb-net", ip: "0.0.0.0", hostname: "fmruby-a1b2c3" }
    s.start
    s.run(1000)
    # Joins: the screen follows within a poll.
    s.env.wifi = { connected: true, ssid: "fmrb-net", ip: "192.0.2.10", hostname: "fmruby-a1b2c3" }
    s.run(1200)
  end

  scene "off" do |s|
    s.env.wifi = nil
    s.start
    s.run(500)
  end

  # An hour up: the uptime's h:mm:ss form.
  scene "long_uptime" do |s|
    s.start
    s.wait(3_601_000)
    s.update
  end
end
