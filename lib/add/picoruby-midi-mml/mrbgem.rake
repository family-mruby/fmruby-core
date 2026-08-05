MRuby::Gem::Specification.new('picoruby-midi-mml') do |spec|
  spec.license = 'MIT'
  spec.author  = 'Toshio Maki'
  spec.summary = 'MML (Music Macro Language) parser and player for picoruby-midi'
  spec.require_name = 'midi-mml'
  spec.add_dependency 'picoruby-midi'
end