# frozen_string_literal: true
#
# The keyboard-only desktop (doc/linux_native, L4) on the native program with
# the sdl_keys machine: 320x170, the bar at the top, apps full screen below
# it. Everything is driven with keys; there is no pointer on this desktop.
#
# The menu lists the editor first, so Enter alone opens it.

Regress::Screens.scene "keys_menu", on: %w[native-keys] do |s|
  s.wait_stable
  s.shot
end

# The editor from the menu, full screen under the bar, and one key typed (the
# standard configuration's Spinel editor, as in 20_editor.rb).
Regress::Screens.scene "keys_editor_type", on: %w[native-keys] do |s|
  s.mark
  s.key "enter"
  s.wait_change
  s.wait_stable(timeout: 20, quiet: 2.0)
  s.mark
  s.key "a"
  s.wait_change
  s.shot
end

# The editor and the shell, then the switcher over the shell: both listed,
# with the menu entry after them.
Regress::Screens.scene "keys_switcher", on: %w[native-keys] do |s|
  s.key "enter"
  s.wait_change
  s.wait_stable(timeout: 20, quiet: 2.0)
  s.key "ctrl+tab"
  s.wait_change
  s.key "down"
  s.key "down"
  s.key "enter"
  s.wait_change
  s.key "down"
  s.key "enter"
  s.wait_change
  s.wait_stable(timeout: 20, quiet: 2.0)
  s.mark
  s.key "ctrl+tab"
  s.wait_change
  s.shot
  s.key "esc"
  s.wait_change
end
