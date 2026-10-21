# rakelib/voice.rake
# The mahjong table's declaration voices (doc/commentary/report/c4.md).
#
#   rake voice:mahjong                     # your own voice with your VOICEVOX -> flash_local/ (not in git)
#   rake voice:mahjong_push IP=<Tab5>      # the same files to a Tab5's /home/voice/mahjong/, no reflash
#   rake voice:mahjong_default             # the bundled Open JTalk "Mei" voice -> flash/ (shipped)
#   rake voice:test                        # host tests of the tools and the app's choice of file

namespace :voice do
  desc "Make the mahjong declarations' WAVs with your VOICEVOX into flash_local/ (VOICEVOX_URL, SPEAKER, STYLE)"
  task :mahjong do
    sh "ruby", File.join(ROOT_DIR, "tools", "mahjong_voice.rb")
  end

  desc "Send the voices made by voice:mahjong to a Tab5's /home/voice/mahjong/ over WiFi (IP=<address>)"
  task :mahjong_push do
    ip = ENV["IP"].to_s
    abort "IP=<the Tab5's address> is needed (the remote desktop's address)" if ip.empty?
    src = File.join(ROOT_DIR, "flash_local", "usr", "share", "sounds", "voice", "mahjong")
    abort "#{src} has no voices: rake voice:mahjong first" if Dir.glob(File.join(src, "*.wav")).empty?
    # The directory as it is (CREDITS.txt with the WAVs; the directories
    # are made as needed). --force: a voice made again may keep its size.
    sh "ruby", File.expand_path("../tools/fmrb_rd_fs.rb", ROOT_DIR), ip, "push", src, "/home/voice/mahjong", "--force"
  end

  desc "Render the bundled mahjong declarations (Open JTalk, the MMDAgent voice Mei, CC BY 3.0) into flash/ (docker)"
  task :mahjong_default do
    sh "ruby", File.join(ROOT_DIR, "tools", "mahjong_voice_default.rb")
  end

  desc "Mahjong voice tests: the tools and the app's choice of file (host Ruby, no docker, no network)"
  task :test do
    sh "ruby", File.join(ROOT_DIR, "test", "mahjong_voice", "run.rb")
  end
end
