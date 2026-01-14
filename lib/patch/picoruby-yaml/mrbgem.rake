MRuby::Gem::Specification.new('picoruby-yaml') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'YAML parser for PicoRuby'

  # Note: Changed from mruby-io to picoruby-fmrb-io for platform independence
  if build.posix?
    if build.vm_mrubyc?
      spec.add_dependency 'picoruby-posix-io'
    else
      spec.add_dependency 'picoruby-fmrb-io'  # Changed from 'mruby-io'
    end
  else
    spec.add_dependency 'picoruby-fmrb-io'    # Use fmrb-io for non-POSIX too
    # spec.add_dependency 'picoruby-filesystem-fat'
    # spec.add_dependency 'picoruby-vfs'
  end
end
