MRuby::Gem::Specification.new('picoruby-fmrb-editor-core') do |spec|
  spec.license = 'MIT'
  spec.authors = ['Katsuhiko Kageyama']
  spec.summary = 'Editor document model (lines, edits, file I/O, highlight cache) in C'

  # The highlight cache calls the Prism-based tokenizer directly (C level), so
  # the editor never builds a Ruby String just to have it coloured.
  spec.add_dependency 'picoruby-syntax-highlight'
  # Completion comes from the type inference engine (ports/esp32/
  # editor_ti_bridge.c). Both are compiled by the CMake component, which has
  # the include paths; the dependency records the relationship and keeps the
  # engine in any build that has the editor.
  spec.add_dependency 'picoruby-ti'
end
