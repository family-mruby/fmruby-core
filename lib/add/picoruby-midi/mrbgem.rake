MRuby::Gem::Specification.new('picoruby-midi') do |spec|
  spec.license = 'MIT'
  spec.author  = 'Toshio Maki'
  spec.summary = 'MIDI protocol layer (parser, scheduler, clock) for PicoRuby'
  spec.require_name = 'midi'
  spec.add_dependency 'picoruby-machine'
end
