MRuby::Gem::Specification.new('picoruby-machine') do |spec|
  spec.license = 'MIT'
  spec.author  = 'HASUMI Hitoshi'
  spec.summary = 'Machine class'

<<<<<<< ours
  # family-mruby: picoruby-io-console is not used; fmrb-io provides IO
  # spec.add_dependency 'picoruby-io-console'

  spec.cc.include_paths << "#{dir}/include"
  if build.vm_mruby?
    # mruby-task include path for task.h and task_hal.h
    spec.cc.include_paths << "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/include"
    # mruby include path for mruby.h
    spec.cc.include_paths << "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/include"
    spec.cc.include_paths << "#{MRUBY_ROOT}/mrbgems/picoruby-mruby/include"
=======
  spec.add_dependency 'picoruby-require'
  spec.add_dependency 'picoruby-io-console'

  if build.posix?
    # The POSIX port runs a dedicated stdin reader thread to emulate the
    # UART/CDC RX interrupt used on bare-metal targets (see ports/posix/machine.c).
    spec.cc.flags << '-pthread'
    spec.linker.flags << '-pthread'
  end

  if build.gems.map(&:name).include?('picoruby-mruby')
    # Workaround:
    #   Locate mruby-io at the top of gem_init.c
    #   to define IO.open earlier than this gems
    if build.posix?
      spec.add_dependency 'mruby-io'
    end
>>>>>>> upstream
  end

  if build.posix?
    cc.defines << "PICORB_PLATFORM_POSIX"
  end

  # family-mruby: mruby-io is NOT used; picoruby-fmrb-io provides IO
  # (mruby-io conflicts with picoruby-fmrb-io)
end
