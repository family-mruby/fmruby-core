# frozen_string_literal: true
#
# The File Manager remembers its folder (doc/app_state): opened, moved into
# the first folder of "/" (app on the sim, whatever sorts first on the native
# program), closed, and opened again -- the shot is the second opening, which
# has to show that folder rather than "/". The state is
# /tmp/file_manager.state.toml, which lasts for one run, so this checks it
# inside one boot. Each scene closes the File Manager again, so the desktop
# comes back as it was.

Regress::Screens.scene "fmgr_remember", on: %w[modern retro native] do |s|
  s.menu :file_manager
  s.wait_change
  s.wait_stable
  s.mark
  s.key "down"
  s.key "enter"
  s.wait_change
  s.wait_stable
  s.key "esc"
  s.wait_change
  s.wait_stable
  s.mark
  s.menu :file_manager
  s.wait_change
  s.park
  s.shot
  s.key "esc"
  s.wait_change
end

# The keys desktop shares the File Manager and its state. The menu lists the
# editor, the shell, then the File Manager.
Regress::Screens.scene "keys_fmgr_remember", on: %w[native-keys] do |s|
  s.wait_stable
  open_fm = lambda do
    s.mark
    s.key "down"
    s.key "down"
    s.key "enter"
    s.wait_change
    s.wait_stable
  end
  open_fm.call
  s.mark
  s.key "down"
  s.key "enter"
  s.wait_change
  s.wait_stable
  s.mark
  s.key "esc"
  s.wait_change
  s.wait_stable
  open_fm.call
  s.shot
  s.mark
  s.key "esc"
  s.wait_change
end
