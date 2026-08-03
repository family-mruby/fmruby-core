MRuby::Gem::Specification.new('picoruby-fmrb-midi') do |spec|
  spec.license = 'MIT'
  spec.authors = ['Katsuhiko Kageyama']
  spec.summary = 'Family mruby MIDI transport for the built-in APU'

  spec.add_dependency 'picoruby-machine'
  spec.add_dependency 'picoruby-midi'
  spec.add_dependency 'picoruby-fmrb-app'
end
