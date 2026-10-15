# rakelib/mahjong.rake
# The mahjong game on this PC (doc/ruby_asterism/report/mj_layout.md): zenohd,
# the game host (asterism-games) and the page server for the browser build,
# up or down in one go. The work is tools/mahjong_local.rb.

namespace :mahjong do
  desc "Start zenohd, the mahjong game host and the page server, print the address (rake wasm:web first)"
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
end
