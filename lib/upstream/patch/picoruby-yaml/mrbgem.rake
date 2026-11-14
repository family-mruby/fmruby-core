MRuby::Gem::Specification.new('picoruby-yaml') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'YAML parser for PicoRuby'

  # family-mruby: use fmrb-io instead of upstream IO dependencies
  spec.add_dependency 'picoruby-fmrb-io'
end

