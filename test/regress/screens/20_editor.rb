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

# Several documents in one editor (doc/editor_multi_file): a second one
# (Ctrl+N) brings the tab row and an empty page, and Ctrl+PageUp goes back to
# the first one's text. Ctrl+PageDown / Ctrl+PageUp stand in for Alt+. /
# Alt+, here: the sim and the native program do not carry Alt. Judged on the
# top of the editor's Window (its menu row, the tab row and the first text
# lines), against a capture of the same run: the first document has to look
# the same each time it comes back.
Regress::Screens.scene "editor_docs", on: %w[modern retro web native modern-mruby retro-mruby] do |s|
  area = [4, 30, 100, 40]
  s.open_editor
  s.mark
  s.text "alpha"
  s.wait_change(area, what: "the first document's text")
  s.key "ctrl+n"
  s.wait_change(area, what: "a second document and the tab row")
  s.text "bravo"
  s.wait_change(area, what: "the second document's text")
  s.key "ctrl+pageup"
  s.wait_change(area, what: "back to the first document")
  first = s.wait_stable
  s.key "ctrl+pagedown"
  s.wait_change(area, what: "the second document again")
  s.key "ctrl+pageup"
  s.wait_same(first, area, what: "the first document as it was")
end
