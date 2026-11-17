# frozen_string_literal: true
#
# The keyboard-only desktop (doc/linux_native, L4) on the native program with
# the sdl_keys machine: 320x170, the bar at the top, apps full screen below
# it. Everything is driven with keys; there is no pointer on this desktop.
#
# The menu lists the editor first, so Enter alone opens it.

Regress::Screens.scene "keys_boot", on: %w[native-keys] do |s|
  s.wait_stable
  s.wait_not_blank("the keys desktop")
end

# The editor from the menu, full screen under the bar, and one key typed (the
# standard configuration's Spinel editor, as in 20_editor.rb).
Regress::Screens.scene "keys_editor_type", on: %w[native-keys] do |s|
  s.mark
  s.key "enter"
  s.wait_change(what: "the editor from the menu")
  s.wait_app_named(/Editor/, "the editor")
  s.wait_stable(timeout: 20, quiet: 2.0)
  s.mark
  s.key "a"
  s.wait_change(s.editor_text_rect, what: "the typed key in the editor's text")
end

# The editor and the shell, then the switcher over the shell: both are
# running, and the switcher comes up and goes again.
Regress::Screens.scene "keys_switcher", on: %w[native-keys] do |s|
  s.key "enter"
  s.wait_change(what: "the editor from the menu")
  s.wait_app_named(/Editor/, "the editor")
  s.wait_stable(timeout: 20, quiet: 2.0)
  s.key "ctrl+tab"
  s.wait_change(what: "the switcher over the editor")
  s.key "down"
  s.key "down"
  s.key "enter"
  s.wait_change(what: "the menu from the switcher")
  s.key "down"
  s.key "enter"
  s.wait_change(what: "the shell from the menu")
  s.wait_app_named(/shell/i, "the shell")
  s.wait_stable(timeout: 20, quiet: 2.0)
  s.wait_for_apps("the editor and the shell running side by side") do |list|
    list.values.grep(/Editor/).any? && list.values.grep(/shell/i).any?
  end
  shell = s.mark
  s.key "ctrl+tab"
  s.wait_change(what: "the switcher over the shell")
  s.key "esc"
  s.wait_same(shell, what: "the shell back after the switcher")
end
