# frozen_string_literal: true
#
# The editor opened from the desktop's menu and one key typed: the check
# CLAUDE.md asks of every sim run of the standard configuration (Spinel
# editor), and of the all-mruby one (the mruby editor). A desktop that comes
# up while only the editor dies is a real failure mode
# (doc/spinel_aot/reports/editor_ivar_layout_bug.md). What is looked at is
# that the key shows: the editor's first text line changes.

Regress::Screens.scene "editor_type", on: %w[modern retro web native modern-mruby retro-mruby] do |s|
  s.open_editor
  s.mark
  s.key "a"
  s.wait_change(s.editor_text_rect, what: "the typed key in the editor's text")
end
