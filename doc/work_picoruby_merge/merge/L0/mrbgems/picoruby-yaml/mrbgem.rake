MRuby::Gem::Specification.new('picoruby-yaml') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'YAML parser for PicoRuby'

<<<<<<< ours
  # family-mruby: use fmrb-io instead of upstream IO dependencies
  spec.add_dependency 'picoruby-fmrb-io'
=======
  if build.posix?
    if build.femtoruby?
      spec.add_dependency 'picoruby-posix-io'
    else
      spec.add_dependency 'mruby-io'
    end
  else
    spec.add_dependency 'picoruby-littlefs'
    spec.add_dependency 'picoruby-vfs'
  end
>>>>>>> upstream
end

