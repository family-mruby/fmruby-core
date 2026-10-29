# frozen_string_literal: true
#
# The mruby apps that use FmrbUI: their main screens with the widgets
# (label, button, toggle, stepper, enum, scrollbar), each in English and
# in Japanese where the app has both, and deliberately long texts (file
# and song names, task names, app names) where FmrbUI will start to fit
# them in app_refactor R6.

module UiScenes
  LONG_LOG = "[W] services: tts: the answer from the server did not come within the time limit, giving up after three tries"

  module_function

  def log_lines
    lines = []
    40.times do |i|
      lines << case i % 4
               when 0 then "[I] kernel: app #{i} spawned (pid #{i + 3})"
               when 1 then "[D] gfx: frame #{i} took #{i % 7} ms"
               when 2 then i == 10 ? LONG_LOG : "[W] wifi: rssi -#{60 + i} dBm"
               else "[E] app: something failed at step #{i}"
               end
    end
    lines
  end

  # The registry the store reads (tools/registry.rb in family-mruby-apps).
  def registry
    rows = ["1"]
    apps = [
      ["clock", "1.2.0", "Clock", "時計", "A clock with an alarm", "目覚まし付きの時計", "tool"],
      ["a_rather_long_app_identifier", "10.20.30", "An application with a very long name indeed",
       "とても長い名前のアプリケーションです本当に", "A description that goes on and on past the edge of the window",
       "窓の端を越えてずっと続く説明文がここに入ります", "game"],
      ["tetris", "0.9.1", "Blocks", "ブロック", "Falling blocks", "落ちものパズル", "game"]
    ]
    apps.each do |a|
      rows << (["A"] + a + ["author-a", "retro,modern,web", "200", "150", "100", "120", "0" * 64, "apps/#{a[0]}/",
                            "thumb.png", "100"]).join("\t")
      rows << ["F", "#{a[0]}.app.rb", "1234"].join("\t")
      rows << ["F", "#{a[0]}.app.toml", "120"].join("\t")
    end
    rows.join("\n") + "\n"
  end

  # A PNG header is all the stand-in reads of a picture (its size).
  def png(w, h) = "\x89PNG\r\n\x1a\n".b + [13].pack("N") + "IHDR" + [w, h].pack("NN") + "\x08\x02\x00\x00\x00".b

  # A copy of a shipped file under another (long) name.
  def copy_as(s, from, to)
    s.seed(to, File.binread(File.join(Regress::CORE, "flash", from)))
  end
end

Regress.app "logviewer", "main/prebuild_scripts/default_app/logviewer.app.rb" do
  window w: 300, h: 200, name: "LogViewer", resizable: true   # fmrb_app_spawner.c

  scene "tail" do |s|
    s.env.extra[:log] = UiScenes.log_lines
    s.start
    s.update
  end

  scene "wrap_and_level" do |s|
    s.env.extra[:log] = UiScenes.log_lines
    s.start
    s.key "w"
    x0 = s.ivar(:@user_area_x0)
    y0 = s.ivar(:@user_area_y0)
    s.click(x0 + 6, y0 + 4)
    s.update
    s.wheel(1)
    s.update
  end
end

Regress.app "monitor", "main/prebuild_scripts/default_app/monitor.app.rb" do
  window w: 240, h: 200, name: "Monitor"

  def self.nav(s, right)
    x = s.ivar(:@content_x)
    w = s.ivar(:@content_w)
    s.click(right ? x + w - 3 : x + 3, s.ivar(:@nav_y) + 3)
  end

  scene "pages" do |s|
    s.start
    s.run(2000)
    3.times do
      nav(s, true)
      s.run(1000)
    end
  end

  scene "tasks_stop" do |s|
    s.start
    s.update
    nav(s, false)
    s.update
    s.tap(:kill0)
    s.update
    s.tap(:kill0)
    s.control({ "cmd" => "kill_result", "pid" => 4, "ok" => true, "info" => "a_rather_long_application_name" })
    s.update
  end
end

Regress.app "appstore", "main/prebuild_scripts/default_app/appstore.app.rb" do
  window w: 280, h: 190, name: "App Store", resizable: true   # fmrb_app_spawner.c

  def self.web(s)
    base = "https://raw.githubusercontent.com/family-mruby/family-mruby-apps/main/"
    s.env.extra[:http] = { "#{base}registry.tsv" => UiScenes.registry }
    %w[clock a_rather_long_app_identifier tetris].each do |id|
      s.env.extra[:http]["#{base}apps/#{id}/thumb.png"] = UiScenes.png(32, 24)
    end
  end

  scene "find" do |s|
    web(s)
    s.start
    s.update(3)
  end

  scene "find_long", lang: "ja" do |s|
    web(s)
    s.start
    s.update(2)
    s.key :down
    s.update(3)
  end

  scene "installed_and_offline" do |s|
    s.start
    s.update
    s.tap(:f_inst)
    s.update
  end
end

Regress.app "nsf_player", "flash/app/tool/nsf_player.app.rb" do
  scene "list" do |s|
    s.start
    s.update
  end

  scene "long_names" do |s|
    UiScenes.copy_as(s, "usr/share/sounds/nsf/encounter.nsf",
                     "/usr/share/sounds/nsf/an_extremely_long_file_name_for_a_chiptune_album.nsf")
    UiScenes.copy_as(s, "usr/share/sounds/nsf/test.nsf", "/usr/share/sounds/nsf/zz_とても長い日本語のファイル名の曲.nsf")
    s.start
    s.key :down
    s.update
    s.tap(:play)
    s.update
    s.tap(:track, at: :right)
    s.update
  end
end

Regress.app "smf_player", "flash/app/tool/smf_player.app.rb" do
  gems(*Regress::Helpers::MIDI_GEMS)
  prelude { |_s| Regress::Helpers.midi_serial }

  scene "list" do |s|
    s.start
    s.update
  end

  scene "playing", lang: "ja" do |s|
    UiScenes.copy_as(s, "usr/share/sounds/midi/scale.mid",
                     "/usr/share/sounds/midi/a_very_long_name_of_a_standard_midi_file_song.mid")
    s.start
    s.key :enter
    s.run(600)
    s.tap(:tempo, at: :right)
    s.tap(:out)
    s.update
    s.tap(:inst_mode)
    s.tap(:inst_up)
    s.update
  end
end

Regress.app "midi_apu", "flash/app/demo/midi_apu.app.rb" do
  gems(*Regress::Helpers::MIDI_GEMS)
  prelude { |_s| Regress::Helpers.midi_serial }

  scene "modes" do |s|
    s.start
    s.update
    %i[chord drums bgm fast out].each do |id|
      s.tap(id)
      s.run(300)
    end
  end

  scene "smf" do |s|
    s.start
    s.tap(:smf)
    s.run(800)
  end
end

Regress.app "kamon", "flash/app/demo/kamon.app.rb" do
  scene "start", lang: "ja" do |s|
    s.start
    s.update
  end

  scene "change" do |s|
    s.start
    s.tap(:m_hana)
    s.tap(:c_ring)
    s.tap(:count, at: :right)
    s.tap(:size, at: :left)
    s.tap(:invert)
    s.update
  end
end

Regress.app "picorabbit", "flash/app/tool/picorabbit.app.rb" do
  gems "picoruby-fmrb-picorabbit"

  scene "menu" do |s|
    s.start
    s.update
  end

  scene "menu_long_names" do |s|
    s.seed("/home/slides/a_presentation_with_a_really_long_file_name_that_goes_on.md", "# Title\n\nbody\n")
    s.seed("/home/slides/日本語のとても長い名前の発表資料です.md", "# 題\n\n本文\n")
    s.start
    s.update
  end

  scene "present" do |s|
    s.start
    s.key :down
    s.key :enter
    s.update
    s.key :space
    s.update
    s.key :right
    s.update
    s.key "i"
    s.update
  end
end

Regress.app "picoruby_demo", "flash/app/demo/picoruby.app.rb" do
  scene "pages" do |s|
    s.start
    s.update
    4.times do
      s.tap(:next)
      s.update(2)
    end
  end

  scene "pages_ja", lang: "ja" do |s|
    s.start
    s.update
    s.key :right
    s.update(2)
  end
end
