# frozen_string_literal: true
#
# mruby apps whose real pixels the draw records (rake regress) cannot show:
# LovyanGFX's round rectangles, ellipses and triangles, the Japanese fonts
# (misaki 8, efont 12), FmrbUI's widgets with Japanese labels, and a
# fullscreen app of sprites and BMP scenery (walk_scroller, Modern only:
# Retro's storage leaves it out).

Regress::Screens.scene "picoruby_shapes", on: %w[modern retro] do |s|
  s.open_app "/app/demo/picoruby.app.rb"
  s.shot
end

Regress::Screens.scene "picoruby_fonts_ja", on: %w[modern retro] do |s|
  s.open_app "/app/demo/picoruby.app.rb"
  s.wait_stable
  s.key "tab"     # Shapes -> Fonts
  s.wait_change
  s.key "right"   # page 2: set_font(:ja, 8) and (:ja, 12)
  s.wait_change
  s.park
  s.shot
end

Regress::Screens.scene "kamon", on: %w[modern retro web] do |s|
  s.open_app "/app/demo/kamon.app.rb", web_index: 11
  s.shot
end

Regress::Screens.scene "walk_scroller", on: %w[modern] do |s|
  s.open_app "/app/demo/walk_scroller.app.rb"
  s.shot "start"
  s.key "n"       # on to the next mark: the scenery scrolls, the walker
  s.wait_change   # walks there and the mark's title comes up
  s.shot "next", quiet: 2.5, timeout: 15
end
