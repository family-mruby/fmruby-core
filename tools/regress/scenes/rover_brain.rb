# frozen_string_literal: true
#
# rover_brain (flash/app/test/rover_brain.app.rb), one of the five apps
# app_refactor R6 moves over: connecting, connected with no /odom yet, the
# odometry and a drive call, a go job half done, a failed connect.

module RoverScenes
  CALLER = "fmruby-d4e5f6"

  module_function

  # /odom as rover_cam sends it: [x, y, qx, qy, qz, qw, v, w, stamp].
  def odom(s, x, y, v, w)
    s.net.ros["/odom"]&.call([x, y, 0.0, 0.0, 0.0, 1.0, v, w, s.now / 1000.0], nil)
  end
end

Regress.app "rover_brain", "flash/app/test/rover_brain.app.rb" do
  scene "connecting" do |s|
    s.start
  end

  scene "no_odom" do |s|
    s.start
    s.run(500)
  end

  scene "drive" do |s|
    s.start
    s.update
    RoverScenes.odom(s, 0.0, 0.0, 0.0, 0.0)
    s.net.call_exposed("rover", :drive, 0.2, 0.5, from: RoverScenes::CALLER)
    4.times do |i|
      RoverScenes.odom(s, 0.01 * i, 0.0, 0.2, 0.5)
      s.run(100)
    end
  end

  scene "go_job" do |s|
    s.start
    s.update
    RoverScenes.odom(s, 0.0, 0.0, 0.0, 0.0)
    s.net.call_exposed("rover", :go, 1.0, from: RoverScenes::CALLER)
    5.times do |i|
      RoverScenes.odom(s, 0.1 * (i + 1), 0.0, 0.3, 0.0)
      s.run(100)
    end
  end

  scene "connect_failed" do |s|
    s.net.refuse = "tls/192.0.2.1:7447 needs a credentials .tar in /home/asterism"
    s.start
    s.run(300)
  end
end
