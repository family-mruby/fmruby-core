# frozen_string_literal: true
#
# The desktop came up (the runner waited for the boot's "Boot complete"):
# the screen shows something, not one colour and not black.

Regress::Screens.scene "boot", on: %w[modern retro web native modern-mruby retro-mruby] do |s|
  s.wait_not_blank("the desktop")
end
