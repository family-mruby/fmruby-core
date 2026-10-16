# rakelib/mahjong.rake
# The mahjong game on this PC (doc/ruby_asterism/report/mj_layout.md): zenohd,
# the game host (asterism-games) and the page server for the browser build,
# up or down in one go. The work is tools/mahjong_local.rb.

namespace :mahjong do
  desc "Start zenohd, the mahjong game host and the page server, print the address (rake wasm:dist first)"
  task :up do
    sh "ruby #{File.join(ROOT_DIR, 'tools', 'mahjong_local.rb')} up"
  end

  desc "Show what of the local mahjong setup is running"
  task :status do
    sh "ruby #{File.join(ROOT_DIR, 'tools', 'mahjong_local.rb')} status"
  end

  desc "Stop the game host and the page server started by mahjong:up, and zenohd (KEEP_ZENOHD=1 keeps it)"
  task :down do
    sh "ruby #{File.join(ROOT_DIR, 'tools', 'mahjong_local.rb')} down"
  end

  # The table app's screen against its worst cases (doc/ruby_asterism/report/
  # mj5.md): the app's own files under host Ruby with a recording stand-in
  # for the graphics; fails when something at 1280x720 or 640x360 leaves the
  # screen or its part of it (426x240 warns). CASE=<name> SIZE=640x360 V=1.
  desc "Check the mahjong table's screen for overflow in its worst cases (host Ruby)"
  task :overflow do
    args = []
    args += ["--case", ENV["CASE"]] if ENV["CASE"]
    args += ["--size", ENV["SIZE"]] if ENV["SIZE"]
    args << "-v" if ENV["V"]
    sh "ruby", File.join(ROOT_DIR, "tools", "mahjong_overflow", "check.rb"), *args
  end
end
