# frozen_string_literal: true
#
# The desktop (system_desktop, mruby in every configuration): at rest, its
# dropdown, the launcher (the icons are BMPs from /app) and the Config
# dialog. Each one is closed again; the runner checks that the desktop
# comes back pixel for pixel.

Regress::Screens.scene "desktop", on: %w[modern retro web modern-mruby retro-mruby] do |s|
  s.shot
end

Regress::Screens.scene "menu", on: %w[modern retro web] do |s|
  s.click(30, 5)
  s.wait_change([0, s.menu_bar_h, 80, 30])
  s.park
  s.shot
  s.key "esc"
end

Regress::Screens.scene "launcher", on: %w[modern retro web] do |s|
  s.menu :launcher
  s.wait_title("the launcher")
  s.park
  s.shot
  s.key "esc"
end

Regress::Screens.scene "config", on: %w[modern retro web] do |s|
  s.menu :config
  s.wait_title("the Config dialog")
  s.park
  s.shot
  s.click_button(s.button_rgb, [(s.w - 280) / 2, 20, 280, s.h - 20]) # Cancel (no Esc here)
end
