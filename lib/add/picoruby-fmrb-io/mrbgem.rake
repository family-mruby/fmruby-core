MRuby::Gem::Specification.new('picoruby-fmrb-io') do |spec|
  spec.license = 'MIT'
  spec.author  = 'FMRuby Project'
  spec.summary = 'Standard IO for FMRuby (platform independent)'

  # Conflicts with POSIX-based IO implementations
  spec.add_conflict 'mruby-io'
  spec.add_conflict 'picoruby-posix-io'
end
