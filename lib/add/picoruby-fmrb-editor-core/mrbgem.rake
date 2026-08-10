MRuby::Gem::Specification.new('picoruby-fmrb-editor-core') do |spec|
  spec.license = 'MIT'
  spec.authors = ['Katsuhiko Kageyama']
  spec.summary = 'Editor document model (lines, edits, file I/O, highlight cache) in C'

  # The highlight cache calls the Prism-based tokenizer directly (C level), so
  # the editor never builds a Ruby String just to have it coloured.
  spec.add_dependency 'picoruby-syntax-highlight'
end
