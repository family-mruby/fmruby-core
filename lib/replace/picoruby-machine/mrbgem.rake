MRuby::Gem::Specification.new('picoruby-machine') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'Machine class'

  # family-mruby: picoruby-io-console is not used; fmrb-io provides IO
  # spec.add_dependency 'picoruby-io-console'

  spec.cc.include_paths << "#{dir}/include"

  if build.posix?
    cc.defines << "PICORB_PLATFORM_POSIX"
  end

  # family-mruby: mruby-io is NOT used; picoruby-fmrb-io provides IO
  # (mruby-io conflicts with picoruby-fmrb-io)
end
