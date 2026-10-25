# frozen_string_literal: true
#
# walk_scroller (flash/app/demo/walk_scroller.app.rb), fullscreen: the
# start with the sample script, steps arriving from a step sensor, the last
# mark (the fireworks), and the tuning panel (T) with the sensor's answers
# and its raw values. Retro and the small boards leave the app out.

module WalkScenes
  NODE = "fmruby-d4e5f6"
  KEY = "asterism/#{NODE}/step/pedometer/step"

  module_function

  def connected(s)
    s.start
    s.update(2)
  end

  def step(s, n, iv = 520)
    s.net.put(KEY, { "steps" => n, "interval_ms" => iv, "cadence" => 115, "event" => "step" })
  end
end

Regress.app "walk_scroller", "flash/app/demo/walk_scroller.app.rb" do
  scene "start" do |s|
    WalkScenes.connected(s)
    s.run(1000)
  end

  scene "walking" do |s|
    WalkScenes.connected(s)
    WalkScenes.step(s, 100)
    s.update
    (101..106).each do |n|
      WalkScenes.step(s, n)
      s.run(520)
    end
    s.run(1500)
  end

  scene "last_mark" do |s|
    WalkScenes.connected(s)
    6.times { s.key "n" }
    s.run(2000)
  end

  scene "tuning" do |s|
    s.host.answer(:state, { "rise_mg" => 150, "min_gap_ms" => 250, "saved" => false })
    s.host.answer(:tune) { |args| { "rise_mg" => 160, "min_gap_ms" => 250, "saved" => false, "args" => args.size } }
    WalkScenes.connected(s)
    WalkScenes.step(s, 1)
    s.update
    s.key "t"
    s.run(500)
    20.times do |i|
      s.net.put("asterism/#{WalkScenes::NODE}/step/pedometer/accel", { "dyn" => 40 + (i * 37) % 200, "dyn_peak" => 60 + (i * 53) % 240 })
      s.run(50)
    end
    s.key :up
    s.run(300)
  end

  scene "status_off" do |s|
    WalkScenes.connected(s)
    s.key "h"
    s.key "d"
    s.run(1200)
  end
end
