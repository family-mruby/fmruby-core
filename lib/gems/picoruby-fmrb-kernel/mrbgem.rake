MRuby::Gem::Specification.new('picoruby-fmrb-kernel') do |spec|
  spec.license = 'MIT'
  spec.authors = ['Katsuhiko Kageyama']
  spec.summary = 'Family mruby OS Kernel API for PicoRuby'

  spec.add_dependency 'picoruby-machine'
end
