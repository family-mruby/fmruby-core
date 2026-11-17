# frozen_string_literal: true
#
# The File Manager remembers its folder (doc/app_state): opened, moved into a
# folder, closed, and opened again -- the second opening has to show the
# folder it was left in, not the one it started in. Both are captures of
# the same run (no baseline): the screen when it was left in the folder,
# and the screen when it opens again. The state is
# /tmp/file_manager.state.toml, so this checks it inside one boot.
#
# It starts in the fixture tree (fixtures.rb, seed_fm: /tmp/regress_fm and the
# state pointing there), never in "/" or /app, so the listing is the same
# whatever apps flash/app holds. There the rows are "..", alpha, beta and
# gamma.txt: two downs select alpha. Each scene closes the File Manager again,
# so the desktop comes back as it was.

Regress::Screens.scene "fmgr_remember", on: %w[modern retro native] do |s|
  area = s.below_menu_bar_rect
  s.seed_fm
  s.menu :file_manager
  s.wait_differs(s.desktop_at_rest, area, what: "the File Manager's Window")
  s.park
  s.wait_stable
  s.mark
  s.key "down"
  s.key "down"
  s.key "enter"
  s.wait_change(area, what: "the File Manager in alpha")
  s.park
  inside = s.wait_stable
  s.key "esc"
  s.wait_same(s.desktop_at_rest, area, what: "the File Manager closed")
  s.menu :file_manager
  s.wait_differs(s.desktop_at_rest, area, what: "the File Manager's Window again")
  s.park
  s.wait_same(inside, area, what: "the File Manager opened again in alpha")
  s.key "esc"
  s.wait_same(s.desktop_at_rest, area, what: "the File Manager closed again")
end

# The keys desktop shares the File Manager and its state. The menu lists the
# editor, the shell, then the File Manager; it opens full screen.
Regress::Screens.scene "keys_fmgr_remember", on: %w[native-keys] do |s|
  s.seed_fm
  s.wait_stable
  open_fm = lambda do |what|
    s.mark
    s.key "down"
    s.key "down"
    s.key "enter"
    s.wait_change(what: what)
    s.wait_stable
  end
  open_fm.call("the File Manager from the menu")
  s.mark
  s.key "down"
  s.key "down"
  s.key "enter"
  s.wait_change(what: "the File Manager in alpha")
  inside = s.wait_stable
  s.mark
  s.key "esc"
  s.wait_change(what: "the File Manager closed")
  s.wait_stable
  open_fm.call("the File Manager from the menu again")
  s.wait_same(inside, what: "the File Manager opened again in alpha")
  s.mark
  s.key "esc"
  s.wait_change(what: "the File Manager closed again")
end
