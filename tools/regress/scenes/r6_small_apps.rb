# frozen_string_literal: true
#
# Three of the five apps app_refactor R6 moves onto FmrbText and the click
# targets: MML (flash/app/demo/mml.app.rb), StackChan Remote
# (flash/app/demo/stackchan_remote.app.rb) and the video player
# (flash/app/modern/video_play.app.rb). Their main screens, each button
# once, and what they send (the publishes, the file selector).

Regress.app "mml", "flash/app/demo/mml.app.rb" do
  gems(*Regress::Helpers::MIDI_GEMS)
  prelude { |_s| Regress::Helpers.midi_serial }

  scene "start" do |s|
    s.start
    s.update
  end

  scene "tunes" do |s|
    s.start
    s.key "2"
    s.run(1000)
    s.key "l"
    s.key "o"
    s.update
    s.key " "
    s.update
  end

  scene "buttons" do |s|
    s.start
    # The rows: x0 + 8, y0 + 26 + 18 * i.
    x0 = s.ivar(:@user_area_x0)
    y0 = s.ivar(:@user_area_y0)
    [3, 5, 7, 6, 8].each do |i|
      s.click(x0 + 20, y0 + 26 + 18 * i + 5)
      s.update
    end
  end

  scene "file", lang: "ja" do |s|
    s.start
    s.key "f"
    s.control({ "cmd" => "file_selected", "path" => "/usr/share/music/round.mml" })
    s.run(500)
    s.control({ "cmd" => "file_selected", "path" => "/home/a_very_long_file_name_for_a_tune.mml" })
    s.update
    s.control({ "cmd" => "file_selected", "path" => nil })
    s.update
  end
end

Regress.app "stackchan_remote", "flash/app/demo/stackchan_remote.app.rb" do
  scene "start" do |s|
    s.start
    s.update
  end

  scene "buttons" do |s|
    s.start
    bs = s.ivar(:@buttons)
    [0, 1, 3, 9, 12].each do |i|
      b = bs[i]
      s.click(b[:x] + 3, b[:y] + 3)
      s.update(2)
    end
  end
end

Regress.app "video_play", "flash/app/modern/video_play.app.rb" do
  on { |t| t.family != "mini" }

  scene "no_file" do |s|
    s.start
    s.update
  end

  scene "playing" do |s|
    s.env.video = { width: 320, height: 176 }
    s.start
    s.run(1200)
    y = s.ivar(:@user_area_y0) + 5
    x0 = s.ivar(:@user_area_x0) + 4
    # Pause, Rewind, Play, the rate (left, then right), Open.
    [1, 2, 0].each { |i| s.click(x0 + i * 48 + 5, y) }
    s.click(x0 + 4 * 48 + 5, y)
    s.click(x0 + 4 * 48 + 5, y, button: 3)
    s.click(x0 + 3 * 48 + 5, y)
    s.control({ "cmd" => "file_selected", "path" => "/mnt/sd/movie/a_rather_long_movie_name_for_the_status_line.mjpg" })
    s.run(1200)
  end
end
