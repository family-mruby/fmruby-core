# Family mruby's default profile of an Asterism node (doc/ruby_asterism,
# V2.5): kind family_mruby, host from the board. Runs when a VM starts,
# before the application, so an application's own Asterism.profile(...)
# changes it further. The icon is the gem's default file,
# /usr/share/asterism/icon.png (flash/usr/share/asterism/; the browser
# bundle stages its own icon there, see rakelib/wasm.rake); it is read when
# the profile is first needed (connect), not here.
module FmrbAsterism
  # FmrbConst::BOARD -> the profile's host. The sim and the browser build
  # say what they are; NARYAv4 is the P4-Nano board.
  HOSTS = { "linux" => "sim", "wasm" => "browser", "naryav4" => "p4nano" }

  def self.host
    board = FmrbConst::BOARD
    HOSTS[board] || board
  end
end

Asterism.profile(kind: :family_mruby, host: FmrbAsterism.host)
